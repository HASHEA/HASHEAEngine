#include "KGFX_RayTracingDx12.h"
#include "KGFX_GraphiceDeviceDx12.h"
#include "KGFX_BufferDx12.h"
#include "KGFX_TextureViewDX12.h"
#include "KGFX_SamplerDX12.h"
#include "KEnginePub/Private/comm/KGFX_StaticConstBuffer.h"
#include "../comm/KGFX_ShaderHelper.h"
#include "KGFX_DXCComplieDx12.h"
#include "KGFX_HashFunDX12.h"
#include <regex>
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
#define RayTracingAccelerationStructureAlignment D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
#define RayTracingScratchBufferAlignment D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
#define RAYTRACING_BINDLESSSPACE 0
#define RAYTRACING_GLOBALBINDINGSPACE 1
#define RAYTRACING_LOCALBINDINGSPACE 2
#define RAYTRACING_VERTEXBUFFER_REGISTER 0
#define RAYTRACING_INDEXBUFFER_REGISTER 1
#define RAYTRACING_CUSTONDATABUFFER_REGISTER 0
#define RAYTRACING_COMPILELIBNAME "lib_6_6"
#define RAYTRACING_CUSTOMDATA_NAME "HitGroupCustomDataBuffer"

    static const uint32_t RootDescriptorTableCostGlobal = 1;
    static const uint32_t RootDescriptorTableCostLocal = 2;
    static const uint32_t RootConstantCost = 1;
    static const uint32_t RootDescriptorCost = 2;

    template <typename T>
    constexpr T Align(T Val, uint64_t Alignment)
    {
        return (T)(((uint64_t)Val + Alignment - 1) & ~(Alignment - 1));
    }

    template< typename t_A, typename t_B >
    inline t_A RoundUpToNextMultiple(const t_A& a, const t_B& b)
    {
        return ((a - 1) / b + 1) * b;
    }

    static std::wstring CharToWChar(const char* str)
    {
        assert(str != nullptr);

        size_t Len = mbstowcs(nullptr, str, 0);
        if (Len == (size_t)-1)
            return std::wstring();

        std::wstring Result(Len, 0);
        mbstowcs(&Result[0], str, (int)Len);
        if (!Result.empty() && Result.back() == L'\0')
            Result.pop_back();

        return Result;
    }

    static ShaderStageType ConvertShaderStage(enumRayTracingShaderType sType)
    {
        ShaderStageType StageType = ShaderStageType::AllGraphics;
        switch (sType)
        {
        case KRT_ST_RAY_GEN:
            StageType = ShaderStageType::RayGeneration;
            break;
        case KRT_ST_HIT_GROUP:
            StageType = ShaderStageType::HitGroup;
            break;
        case KRT_ST_MISS:
            StageType = ShaderStageType::Miss;
            break;
        case KRT_ST_CALLABLE:
            StageType = ShaderStageType::Callable;
            break;
        default:
            assert(false);
        }

        return StageType;
    }

    std::unordered_set<std::wstring> g_paramNamePool;

    const wchar_t* GetWcharByPool(const wchar_t* pName)
    {
        const wchar_t* inPoolName = nullptr;
        if (pName)
        {
            auto                        it = g_paramNamePool.find(pName);
            if (it == g_paramNamePool.end())
            {
                std::pair<std::unordered_set<std::wstring>::iterator, bool> ret = g_paramNamePool.insert(pName);
                std::unordered_set<std::wstring>::iterator                  i = ret.first;
                inPoolName = i->c_str();
            }
            else
            {
                inPoolName = it->c_str();
            }
        }
        return inPoolName;
    }

    struct DXILLibrary
    {
        DXILLibrary() = default;

        void InitFromDXIL(const void* pByteCode, uint32_t ByteCodeSize, const LPCWSTR* OriginName, const LPCWSTR* ExportName, uint32_t ExportCount)
        {
            vecExportDescs.resize(ExportCount);

            for (uint32_t Count = 0; Count < ExportCount; ++Count)
            {
                vecExportDescs[Count].Name = (ExportName[Count]);
                vecExportDescs[Count].Flags = D3D12_EXPORT_FLAG_NONE;
                vecExportDescs[Count].ExportToRename = (OriginName[Count]);
            }

            DxilLibDesc.DXILLibrary.pShaderBytecode = pByteCode;
            DxilLibDesc.DXILLibrary.BytecodeLength = ByteCodeSize;
            DxilLibDesc.NumExports = ExportCount;
            DxilLibDesc.pExports = vecExportDescs.data();
        }

        D3D12_STATE_SUBOBJECT GetSubObject()const
        {
            D3D12_STATE_SUBOBJECT SubObject;
            SubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            SubObject.pDesc = &DxilLibDesc;

            return SubObject;
        }

        std::vector<D3D12_EXPORT_DESC> vecExportDescs;
        D3D12_DXIL_LIBRARY_DESC DxilLibDesc{};
    };

    static std::wstring GetCollectionTypeName(RayTracingPipelineCacheDx12::CollectionType Type)
    {
        switch (Type)
        {
        case RayTracingPipelineCacheDx12::CollectionType::RayGen:
            return L"RayGen";
        case RayTracingPipelineCacheDx12::CollectionType::Miss:
            return L"Miss";
        case RayTracingPipelineCacheDx12::CollectionType::HitGroup:
            return L"HitGroup";
        case RayTracingPipelineCacheDx12::CollectionType::Callable:
            return L"Callable";
        case RayTracingPipelineCacheDx12::CollectionType::Unknown:
        default:
            return L"Unknown";

        }
    }

    static RayTracingShaderIdentifier GetShaderIdentifier(ID3D12StateObjectProperties* pStateObjectProperties, LPCWSTR ExportName)
    {
        RayTracingShaderIdentifier ResultShaderIdentifier;
        void* pData = nullptr;
        KGLOG_ASSERT_EXIT(pStateObjectProperties);

       pData = pStateObjectProperties->GetShaderIdentifier(ExportName);
       ResultShaderIdentifier.SetData(pData);

    Exit0:
        return ResultShaderIdentifier;
    }

    static RayTracingShaderIdentifier GetShaderIdentifier(ID3D12StateObject* pStateObject, LPCWSTR ExportName)
    {
        ID3D12StateObjectProperties* pPipelineProperties = nullptr;
        RayTracingShaderIdentifier ResultShaderIdentifier{};
        HRESULT hrRetCode = pStateObject->QueryInterface(IID_PPV_ARGS(&pPipelineProperties));
        KGLOG_COM_ASSERT_EXIT(hrRetCode);
        ResultShaderIdentifier = GetShaderIdentifier(pPipelineProperties, ExportName);
    Exit0:
        SAFE_RELEASE(pPipelineProperties);
        return ResultShaderIdentifier;
    }

    static ID3D12StateObject* CreateRayTracingStateObject(
        std::vector<DXILLibrary>& vecDxilLibrarys,
        std::vector< LPCWSTR>& vecExportNames,
        std::vector<D3D12_HIT_GROUP_DESC>& vecHitGroups,
        std::vector< ID3D12RootSignature*>& vecLocalRootSignatures,
        std::vector<uint32_t>& vecLocalRootSignatureAssociations,
        std::vector< D3D12_EXISTING_COLLECTION_DESC>& vecExistingCollections,
        ID3D12RootSignature* pGlobalRootSignature,
        uint32_t MaxAttributeSizeInBytes,
        uint32_t MaxPayloadSizeInBytes,
        D3D12_STATE_OBJECT_TYPE StateObjectType
    )
    {
        HRESULT hrRetCode = E_FAIL;
        ID3D12StateObject* pResultStateObject = nullptr;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        uint32_t SubIndex = 0;
        std::vector< D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> vecExportAssociations;
        std::vector<D3D12_STATE_SUBOBJECT> vecSubObjects;
        // 1. D3D12_RAYTRACING_SHADER_CONFIG
        // 2. D3D12_RAYTRACING_PIPELINE_CONFIG
        // 3. D3D12_STATE_OBJECT_CONFIG
        // 4. D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION
        // 5. GlobalRootSignature
        vecSubObjects.resize(5 + vecDxilLibrarys.size() + vecHitGroups.size() + vecLocalRootSignatures.size() + vecExportNames.size() + vecExistingCollections.size());
        vecExportAssociations.resize(vecExportNames.size());

        for (uint32_t Index = 0; Index < vecDxilLibrarys.size(); ++Index)
        {
            vecSubObjects[SubIndex++] = vecDxilLibrarys[Index].GetSubObject();
        }

        D3D12_RAYTRACING_SHADER_CONFIG ShaderConfig{};
        ShaderConfig.MaxAttributeSizeInBytes = 8;
        ShaderConfig.MaxPayloadSizeInBytes = MaxPayloadSizeInBytes;
        uint32_t ShaderConfigIndex = SubIndex;
        vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &ShaderConfig };

        D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION Association{};
        Association.NumExports = (UINT)vecExportNames.size();
        Association.pExports = vecExportNames.data();
        Association.pSubobjectToAssociate = &vecSubObjects[ShaderConfigIndex];
        vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &Association };

        for (uint32_t Index = 0; Index < vecHitGroups.size(); ++Index)
        {
            vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &vecHitGroups[Index] };
        }

        vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &pGlobalRootSignature };

        uint32_t LocalSignatureBaseIndex = SubIndex;
        for (uint32_t Index = 0; Index < vecLocalRootSignatures.size(); ++Index)
        {
            vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &vecLocalRootSignatures[Index] };
        }

        for (uint32_t Index = 0; Index < vecExportNames.size(); ++Index)
        {
            uint32_t LocalSignatureIndex = LocalSignatureBaseIndex + (vecLocalRootSignatureAssociations.empty() ? 0 : vecLocalRootSignatureAssociations[Index]);
            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION& Association = vecExportAssociations[Index];
            Association.NumExports = 1;
            Association.pExports = &vecExportNames[Index];
            Association.pSubobjectToAssociate = &vecSubObjects[LocalSignatureIndex];

            vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &Association };
        }

        D3D12_RAYTRACING_PIPELINE_CONFIG PipelineConfig;
        PipelineConfig.MaxTraceRecursionDepth = 2;
        vecSubObjects[SubIndex++] = { D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &PipelineConfig };

        D3D12_STATE_OBJECT_CONFIG StateObjectConfig = {};
        StateObjectConfig.Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
        vecSubObjects[SubIndex++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &StateObjectConfig };

        for (uint32_t Index = 0; Index < vecExistingCollections.size(); ++Index)
        {
            vecSubObjects[SubIndex++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &vecExistingCollections[Index] };
        }

        D3D12_STATE_OBJECT_DESC Desc{};
        Desc.NumSubobjects = SubIndex;
        Desc.pSubobjects = vecSubObjects.data();
        Desc.Type = StateObjectType;

        hrRetCode = pGraphicDevice->GetDXDevice5()->CreateStateObject(&Desc, IID_PPV_ARGS(&pResultStateObject));
        KG_COM_PROCESS_ERROR(hrRetCode);

    Exit0:
        return pResultStateObject;
    }

    static RayTracingShaderIdentifier GetShaderIdentifier(ID3D12StateObjectProperties* pPipelineProperties, LPWSTR ExportName)
    {
        RayTracingShaderIdentifier ResultShaderIdentifier;
        const void* pData = pPipelineProperties->GetShaderIdentifier(ExportName);
        ASSERT(pData != nullptr);
        ResultShaderIdentifier.SetData(pData);
        return ResultShaderIdentifier;
    }

    static BOOL SetRayTracingGlobalShaderResources(
        IKGFX_RenderContext* pRenderContext,
        const RayTracingShaderDx12* pRayGenShader,
        RayTracingShaderBindingTableDx12* pShaderBindingTable,
        IKGFX_BufferView** ppBufferViews, uint32_t NumBufferViews,
        IKGFX_TextureView** ppSRVs, uint32_t NumSRVs,
        IKGFX_TextureView** ppUAVs, uint32_t NumUAVs,
        IKGFX_SamplerBindlessView** ppSamplers, uint32_t NumSamplers,
        uint32_t LooseParameterDataSize, const void* pLooseParameterData
    )
    {
        BOOL nRetCode = FALSE;
        BOOL nResult = FALSE;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        RayTracingRootSignature* pRootSignature = nullptr;
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        KGFX_CommandBufferDX12Impl* Dx12CommandList = (KGFX_CommandBufferDX12Impl*)pRenderContext;
        ID3D12GraphicsCommandList4* pCommandList4 = Dx12CommandList->GetD3D12CommandList4();
        KGLOG_ASSERT_EXIT(pRayGenShader);
        KGLOG_ASSERT_EXIT(pBindlessHeapManager);

        //pRootSignature = pRayGenShader->GetRootSignature();
        //KGLOG_ASSERT_EXIT(pRayGenShader);

        if (NumBufferViews > 0)
        {
            uint32_t CBVStartRootIndex = pRootSignature->CBVBindSlot();
            for (uint32_t i = 0; i < NumBufferViews; i++)
            {
                IKGFX_BufferView* pBufferView = ppBufferViews[i];
                D3D12_GPU_VIRTUAL_ADDRESS Address = pBufferView->GetResource()->GetBufferDeviceAddress();
                pCommandList4->SetComputeRootConstantBufferView(CBVStartRootIndex + i, Address);
            }
        }

        if (NumSRVs > 0)
        {
            uint32_t SRVStartRootIndex = pRootSignature->SRVBindSlot();
            uint32_t StartBindlessIndex = ppSRVs[0]->GetBindlessHandle();
            for (uint32_t i = 0; i < NumSRVs; i++)
            {
                IKGFX_TextureView* pSRV = ppSRVs[i];
                D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor;
                BindlessDescriptor BindlessDescriptor;
                uint32_t BindlessIndex = pSRV->GetBindlessHandle();
                CPUDescriptor.ptr = ((KGFX_TextureViewDX12*)(pSRV))->GetNativeHandle();
                BindlessDescriptor.Index = BindlessIndex;
                BindlessDescriptor.Type = BindlessHeapType::Standard;
                pBindlessHeapManager->CopyDescriptorToBindless(BindlessDescriptor, CPUDescriptor);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = pBindlessHeapManager->GetGPUHandle(BindlessHeapType::Standard, StartBindlessIndex);
            pCommandList4->SetComputeRootDescriptorTable(SRVStartRootIndex, GPUDescriptor);
        }

        if (NumUAVs > 0)
        {

            uint32_t UAVStartRootIndex = pRootSignature->UAVBindSlot();
            uint32_t StartBindlessIndex = ppUAVs[0]->GetBindlessHandle();
            for (uint32_t i = 0; i < NumUAVs; i++)
            {
                IKGFX_TextureView* pUAV = ppUAVs[i];
                D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor;
                BindlessDescriptor BindlessDescriptor;
                uint32_t BindlessIndex = pUAV->GetBindlessHandle();
                CPUDescriptor.ptr = ((KGFX_TextureViewDX12*)(pUAV))->GetNativeHandle();
                BindlessDescriptor.Index = BindlessIndex;
                BindlessDescriptor.Type = BindlessHeapType::Standard;
                pBindlessHeapManager->CopyDescriptorToBindless(BindlessDescriptor, CPUDescriptor);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = pBindlessHeapManager->GetGPUHandle(BindlessHeapType::Standard, StartBindlessIndex);
            pCommandList4->SetComputeRootDescriptorTable(UAVStartRootIndex, GPUDescriptor);
        }

        if (NumSamplers > 0)
        {
            uint32_t SamplerStartRootIndex = pRootSignature->SamplerBindSlot();
            uint32_t StartBindlessIndex = ppSamplers[0]->GetBindlessHandle();
            for (uint32_t i = 0; i < NumSamplers; i++)
            {
                IKGFX_SamplerBindlessView* pSampler = ppSamplers[i];
                D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor;
                BindlessDescriptor BindlessDescriptor;
                uint32_t BindlessIndex = pSampler->GetBindlessHandle();
                CPUDescriptor.ptr = ((KGFX_SamplerBindlessViewDX12*)(pSampler))->GetResource()->GetNativeHandle();
                BindlessDescriptor.Index = BindlessIndex;
                BindlessDescriptor.Type = BindlessHeapType::Sampler;
                pBindlessHeapManager->CopyDescriptorToBindless(BindlessDescriptor, CPUDescriptor);
            }
            D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = pBindlessHeapManager->GetGPUHandle(BindlessHeapType::Sampler, StartBindlessIndex);
            pCommandList4->SetComputeRootDescriptorTable(SamplerStartRootIndex, GPUDescriptor);
        }

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    static BOOL SetRayTracingGlobalShaderResources(
        IKGFX_RenderContext* pRenderContext,
        const RayTracingShaderDx12* pRayGenShader,
        RayTracingShaderBindingTableDx12* pShaderBindingTable,
        IKGFX_BufferView** ppBufferViews, uint32_t NumBufferViews,
        std::vector<uint64_t>& vecResourceHandles, uint32_t NumSRVs, uint32_t NumUAVs,
        uint32_t StartIndex,
        uint32_t LooseParameterDataSize, const void* pLooseParameterData
    )
    {
        BOOL nRetCode = FALSE;
        BOOL nResult = FALSE;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        RayTracingRootSignature* pRootSignature = pGraphicDevice->GetRayTracingGlobalRootSignature();
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        KGFX_CommandBufferDX12Impl* Dx12CommandList = (KGFX_CommandBufferDX12Impl*)pRenderContext;
        ID3D12GraphicsCommandList4* pCommandList4 = Dx12CommandList->GetD3D12CommandList4();
        uint32_t ResourceCount = NumSRVs + NumUAVs;
        KGLOG_ASSERT_EXIT(pRayGenShader);
        KGLOG_ASSERT_EXIT(pBindlessHeapManager);

        //pRootSignature = pRayGenShader->GetRootSignature();
        //KGLOG_ASSERT_EXIT(pRayGenShader);

        if (NumBufferViews > 0)
        {
            uint32_t CBVStartRootIndex = pRootSignature->CBVBindSlot();
            for (uint32_t i = 0; i < NumBufferViews; i++)
            {
                IKGFX_BufferView* pBufferView = ppBufferViews[i];
                D3D12_GPU_VIRTUAL_ADDRESS Address = pBufferView->GetResource()->GetBufferDeviceAddress();
                pCommandList4->SetComputeRootConstantBufferView(CBVStartRootIndex + i, Address);
            }
        }

        if (ResourceCount > 0)
        {
            if (NumSRVs > 0)
            {
                uint32_t SRVStartRootIndex = pRootSignature->SRVBindSlot();
                uint32_t SRVStartIndex = StartIndex;
                for (uint32_t i = 0; i < NumSRVs; i++)
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor;
                    BindlessDescriptor Descriptor;
                    CPUDescriptor.ptr = vecResourceHandles[i];
                    Descriptor.Index = SRVStartIndex + i;
                    Descriptor.Type = BindlessHeapType::Standard;
                    pBindlessHeapManager->CopyDescriptorToBindless(Descriptor, CPUDescriptor);
                }
                D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = pBindlessHeapManager->GetGPUHandle(BindlessHeapType::Standard, SRVStartIndex);
                pCommandList4->SetComputeRootDescriptorTable(SRVStartRootIndex, GPUDescriptor);
            }

            if (NumUAVs > 0)
            {
                uint32_t UAVStartRootIndex = pRootSignature->UAVBindSlot();
                uint32_t UAVStartIndex = StartIndex + NumSRVs;
                for (uint32_t i = 0; i < NumUAVs; i++)
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptor;
                    BindlessDescriptor Descriptor;
                    CPUDescriptor.ptr = vecResourceHandles[i + NumSRVs];
                    Descriptor.Index = UAVStartIndex + i;
                    Descriptor.Type = BindlessHeapType::Standard;
                    pBindlessHeapManager->CopyDescriptorToBindless(Descriptor, CPUDescriptor);
                }
                D3D12_GPU_DESCRIPTOR_HANDLE GPUDescriptor = pBindlessHeapManager->GetGPUHandle(BindlessHeapType::Standard, UAVStartIndex);
                pCommandList4->SetComputeRootDescriptorTable(UAVStartRootIndex, GPUDescriptor);
            }
        }

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    static bool SetRayTracingHitGroup(
        IKGFX_RenderContext* pRenderContext,
        RayTracingShaderBindingTableDx12* pShaderBindingTable,
        RayTracingPipelineStateDx12* pPipeline,
        const KRayTracingShaderBinding& BindingData
    )
    {
        bool nResult = FALSE;
        bool nRetCode = FALSE;
        RayTracingShaderDx12* pShader = nullptr;
        KGLOG_ASSERT_EXIT(pPipeline);
        KGLOG_ASSERT_EXIT(pShaderBindingTable);

        pShader = pPipeline->m_HitGroupShaders.vecRayTracingShaders[BindingData.uShaderIndexInPipeline];
        KGLOG_ASSERT_EXIT(pShader);

        nRetCode = pShaderBindingTable->SetHitGroupBinding(BindingData, pPipeline);
        KGLOG_ASSERT_EXIT(nRetCode);

        //SetRayTracingLocalShaderResources(pRenderContext, pShader, pShaderBindingTable, nullptr, 0, 0, nullptr);

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    RayTracingRootSignature::~RayTracingRootSignature()
    {
        Destroy();
    }

    void RayTracingRootSignature::Destroy()
    {
        SAFE_RELEASE(m_pRootSignature);
    }

    bool RayTracingRootSignature::Init(const RayTracingSignatureDesc& Desc)
    {
        bool bResult = false;
        HRESULT hrRetCode = E_FAIL;
        uint32_t BindingSpace = 0;
        uint32_t RootParam = 0;
        uint32_t RootSignatureSize = 0;
        ID3D12RootSignature* pRootSignature = nullptr;
        std::vector< CD3DX12_ROOT_PARAMETER> vecRootParameters;
        std::vector< CD3DX12_DESCRIPTOR_RANGE> vecRanges;
        vecRanges.reserve(3);
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();

        const uint32_t DescriptorTableCost = Desc.Type == RayTracingBindingType::Global ? RootDescriptorTableCostGlobal : RootDescriptorTableCostLocal;
        const uint32_t CBVOffset = Desc.Type == RayTracingBindingType::Global ? 0 : 1;
        if (Desc.Type == RayTracingBindingType::Global)
        {
            BindingSpace = RAYTRACING_GLOBALBINDINGSPACE;
        }
        else
        {
            BindingSpace = RAYTRACING_LOCALBINDINGSPACE;

            CD3DX12_ROOT_PARAMETER RootParameter;
            RootParameter.InitAsShaderResourceView(RAYTRACING_VERTEXBUFFER_REGISTER, BindingSpace);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += RootDescriptorCost;

            RootParameter.InitAsShaderResourceView(RAYTRACING_INDEXBUFFER_REGISTER, BindingSpace);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += RootDescriptorCost;

            RootParameter.InitAsConstantBufferView(RAYTRACING_CUSTONDATABUFFER_REGISTER, BindingSpace);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += RootDescriptorCost;
        }

        m_ConstantBufferSlot = RootParam;
        if (Desc.Binding.ConstanBuffers > 0)
        {
            for (uint32_t Count = 0; Count < Desc.Binding.ConstanBuffers; ++Count)
            {
                CD3DX12_ROOT_PARAMETER RootParameter;
                RootParameter.InitAsConstantBufferView(Count + CBVOffset, BindingSpace);
                vecRootParameters.push_back(RootParameter);
                RootParam++;
                m_RootSignatureSize += RootDescriptorCost;
            }
        }
        if (Desc.Binding.ShaderResourceViews > 0)
        {
            CD3DX12_ROOT_PARAMETER RootParameter{};
            CD3DX12_DESCRIPTOR_RANGE DescriptorRange{};
            vecRanges.push_back(DescriptorRange);
            auto& range = vecRanges.back();
            m_ShaderResourceViewSlot = RootParam;
            range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Desc.Binding.ShaderResourceViews, 0, BindingSpace);
            RootParameter.InitAsDescriptorTable(1, &range);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += DescriptorTableCost;
        }
        if (Desc.Binding.UnorderedResourceViews > 0)
        {
            CD3DX12_ROOT_PARAMETER RootParameter{};
            CD3DX12_DESCRIPTOR_RANGE DescriptorRange{};
            vecRanges.push_back(DescriptorRange);
            auto& range = vecRanges.back();
            m_UnorderedResourceViewSlot = RootParam;
            range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, Desc.Binding.UnorderedResourceViews, 0, BindingSpace);
            RootParameter.InitAsDescriptorTable(1, &range);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += DescriptorTableCost;
        }
        if (Desc.Binding.Samplers > 0)
        {
            CD3DX12_ROOT_PARAMETER RootParameter{};
            CD3DX12_DESCRIPTOR_RANGE DescriptorRange{};
            vecRanges.push_back(DescriptorRange);
            auto& range = vecRanges.back();
            m_SamplerViewSlot = RootParam;
            range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, Desc.Binding.Samplers, 0, BindingSpace);
            RootParameter.InitAsDescriptorTable(1, &range);
            vecRootParameters.push_back(RootParameter);
            RootParam++;
            m_RootSignatureSize += DescriptorTableCost;
        }

        D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
        rootSigFlags = Desc.Type == RayTracingBindingType::Global ? rootSigFlags : D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(static_cast<uint32_t>(vecRootParameters.size()), vecRootParameters.data(), 0, nullptr, rootSigFlags);
        CComPtr<ID3DBlob> serializedRootSig = nullptr;
        CComPtr<ID3DBlob> errorBlob = nullptr;

        hrRetCode = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);
        if (errorBlob)
        {
            KGLogPrintf(KGLOG_ERR, "%s", static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        KGLOG_COM_ASSERT_EXIT(hrRetCode);

        hrRetCode = pD3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&pRootSignature));
        KGLOG_COM_ASSERT_EXIT(hrRetCode);

        m_pRootSignature = pRootSignature;
        pRootSignature = nullptr;

        bResult = true;
    Exit0:
        if (pRootSignature)
        {
            pRootSignature->Release();
        }
        return bResult;
    }

    RayTracingDescriptorCacheDx12::~RayTracingDescriptorCacheDx12()
    {

    }

    void RayTracingDescriptorCacheDx12::SetGPUDescriptorHeap(DescriptorHeapReference ResourceHeap)
    {
        m_ResourceTable.SetHeapReference(ResourceHeap);
        m_CPUBaseHandle = m_ResourceTable.GetCpuHandle();
    }

    uint32_t RayTracingDescriptorCacheDx12::Allocate(uint32_t NumDescriptors)
    {
        bool bRetCode = false;
        uint32_t BaseIndex = (uint32_t)-1;
        bRetCode = m_ResourceTable.Allocate(NumDescriptors);
        KGLOG_ASSERT_EXIT(bRetCode);

        BaseIndex = m_ResourceTable.m_Offset;

    Exit0:
        return BaseIndex;
    }

    uint32_t RayTracingDescriptorCacheDx12::AllocateDescriptorTable(const D3D12_CPU_DESCRIPTOR_HANDLE* pDescriptors, uint32_t NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type)
    {
        return 0;

    }

    void RayTracingDescriptorCacheDx12::CopyDescriptors(uint32_t BaseIndex, const D3D12_CPU_DESCRIPTOR_HANDLE* pDescriptors, uint32_t NumDescriptors)
    {
        for (uint32_t Count = 0; Count < NumDescriptors; ++Count)
        {

        }
    }

    RayTracingPipelineCacheDx12::Entry::~Entry()
    {
        SAFE_RELEASE(pStateObject);
    }

    RayTracingPipelineCacheDx12::RayTracingPipelineCacheDx12()
    {

    }

    RayTracingPipelineCacheDx12::~RayTracingPipelineCacheDx12()
    {
        for (auto Iter : m_mapCaches)
        {
            delete Iter.second;
            Iter.second = nullptr;
        }

        m_mapCaches.clear();
        SAFE_RELEASE(m_pDefaultRootSignature);
    }

    bool RayTracingPipelineCacheDx12::Init()
    {
        bool bResult = false;
        bool bRetCode = false;
        HRESULT hrRetCode = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        ID3D12RootSignature* pRootSignature = nullptr;
        std::vector< CD3DX12_ROOT_PARAMETER> vecRootParameters;
        D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(static_cast<uint32_t>(vecRootParameters.size()), vecRootParameters.data(), 0, nullptr, rootSigFlags);
        CComPtr<ID3DBlob> serializedRootSig = nullptr;
        CComPtr<ID3DBlob> errorBlob = nullptr;

        hrRetCode = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);
        if (errorBlob)
        {
            KGLogPrintf(KGLOG_ERR, "%s", static_cast<char*>(errorBlob->GetBufferPointer()));
        }
        KGLOG_COM_ASSERT_EXIT(hrRetCode);

        hrRetCode = pD3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&pRootSignature));
        KGLOG_COM_ASSERT_EXIT(hrRetCode);

        m_pDefaultRootSignature = pRootSignature;
        pRootSignature = nullptr;

        bResult = true;
    Exit0:
        SAFE_RELEASE(pRootSignature);
        return bResult;
    }

    void RayTracingPipelineCacheDx12::CompileTask(Entry& InEntry, Key InKey)
    {
        uint32_t HitGroupNum = InEntry.Type == RayTracingPipelineCacheDx12::CollectionType::HitGroup ? 1 : 0;
        RayTracingShaderDx12* pShader = InEntry.pShader;
        ID3D12RootSignature* pGlobalRootSignature = InKey.pGlobalRootSignature;
        ID3D12RootSignature* pLocalRootSignature = InKey.pLocalRootSignature;
        uint64_t ShaderHash = InKey.ShaderHash;
        std::vector<LPCWSTR> vecOriginNames;
        std::vector<LPCWSTR> vecExportNames;
        std::vector< D3D12_HIT_GROUP_DESC> vecHitGroups;
        std::vector<ID3D12RootSignature*> vecLocalRootSignatures;
        std::vector<uint32_t> vecLocalRootSignatureAssociations;
        std::vector< D3D12_EXISTING_COLLECTION_DESC> vecExistingCollections;
        D3D12_HIT_GROUP_DESC HitGroupDesc{};

        vecLocalRootSignatures.push_back(pLocalRootSignature);
        if (InEntry.Type == RayTracingPipelineCacheDx12::CollectionType::HitGroup)
        {
            HitGroupDesc.HitGroupExport = InEntry.GetPrimaryExportName();
            HitGroupDesc.Type = pShader->m_IntersectionEntryPoint.empty() ? D3D12_HIT_GROUP_TYPE_TRIANGLES : D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

            std::wstring CloseHitName = L"CHS" + std::to_wstring(ShaderHash);
            InEntry.vecExportNames.push_back(CloseHitName);
            HitGroupDesc.ClosestHitShaderImport = InEntry.vecExportNames.back().c_str();
            vecOriginNames.push_back(pShader->m_EntryPoint.c_str());
            vecExportNames.push_back(InEntry.vecExportNames.back().c_str());

            if (!pShader->m_AnyHitEntryPoint.empty())
            {
                std::wstring AnyHitName = L"AHS" + std::to_wstring(ShaderHash);
                InEntry.vecExportNames.push_back(AnyHitName);
                HitGroupDesc.AnyHitShaderImport = InEntry.vecExportNames.back().c_str();
                vecOriginNames.push_back(pShader->m_AnyHitEntryPoint.c_str());
                vecExportNames.push_back(InEntry.vecExportNames.back().c_str());
            }

            if (!pShader->m_IntersectionEntryPoint.empty())
            {
                std::wstring IntersectionName = L"ITS" + std::to_wstring(ShaderHash);
                InEntry.vecExportNames.push_back(IntersectionName);
                HitGroupDesc.IntersectionShaderImport = InEntry.vecExportNames.back().c_str();
                vecOriginNames.push_back(pShader->m_IntersectionEntryPoint.c_str());
                vecExportNames.push_back(InEntry.vecExportNames.back().c_str());
            }
            vecHitGroups.push_back(HitGroupDesc);
        }
        else
        {
            vecOriginNames.push_back(pShader->m_EntryPoint.c_str());
            vecExportNames.push_back(InEntry.GetPrimaryExportName());
        }

        DXILLibrary Library;
        std::vector<DXILLibrary> vecLibrarys;
        Library.InitFromDXIL(pShader->GetShaderByteCode(), pShader->GetShaderCodeSize(), vecOriginNames.data(), vecExportNames.data(), (uint32_t)vecOriginNames.size());
        vecLibrarys.push_back(Library);

        InEntry.pStateObject = CreateRayTracingStateObject(
            vecLibrarys,
            vecExportNames,
            vecHitGroups,
            vecLocalRootSignatures,
            vecLocalRootSignatureAssociations,
            vecExistingCollections,
            pGlobalRootSignature,
            InKey.MaxAttributeSizeInBytes,
            InKey.MaxPayloadSizeInBytes,
            D3D12_STATE_OBJECT_TYPE_COLLECTION
        );

        if (InEntry.pStateObject)
        {
            InEntry.ShaderIdentifier = GetShaderIdentifier(InEntry.pStateObject, InEntry.GetPrimaryExportName());
        }
    }

    RayTracingPipelineCacheDx12::Entry* RayTracingPipelineCacheDx12::GetOrCompileShaderToStateObject(RayTracingShaderDx12* pRayTracingShader, ID3D12RootSignature* pGlobalRootSignature, uint32_t MaxAttributeSizeInBytes, uint32_t MaxPayloadSizeInBytes, CollectionType Type)
    {
        Entry* pResultEntry = nullptr;
        Key ShaderKey;
        ShaderKey.ShaderHash = pRayTracingShader->GetHash();
        ShaderKey.MaxAttributeSizeInBytes = 16;// MaxAttributeSizeInBytes;
        ShaderKey.MaxPayloadSizeInBytes = 256;// MaxPayloadSizeInBytes;
        ShaderKey.pGlobalRootSignature = pGlobalRootSignature;
        ShaderKey.pLocalRootSignature = (Type == CollectionType::RayGen || Type == CollectionType::Miss) ? m_pDefaultRootSignature : pRayTracingShader->GetRootSignature()->GetDeviceRootSignature();

        auto Iter = m_mapCaches.find(ShaderKey);
        if (Iter != m_mapCaches.end())
        {
            pResultEntry = Iter->second;
        }
        else
        {
            pResultEntry = new Entry;
            pResultEntry->Type = Type;
            pResultEntry->pShader = pRayTracingShader;
            std::wstring ExportName = GetCollectionTypeName(Type) + std::to_wstring(pRayTracingShader->GetHash());
            pResultEntry->vecExportNames.push_back(ExportName);
            m_mapCaches[ShaderKey] = pResultEntry;

            CompileTask(*pResultEntry, ShaderKey);
        }

        return pResultEntry;
    }

    RayTracingShaderReflector::~RayTracingShaderReflector()
    {
        for (uint32_t i = 0; i < (uint32_t)m_vecUniformBlocks.size(); ++i)
        {
            delete m_vecUniformBlocks[i];
            m_vecUniformBlocks[i] = nullptr;
        }

        for (uint32_t i = 0; i < (uint32_t)m_vecUniformTextures.size(); ++i)
        {
            delete m_vecUniformTextures[i];
            m_vecUniformTextures[i] = nullptr;
        }

        for (uint32_t i = 0; i < (uint32_t)m_vecUniformSamplers.size(); ++i)
        {
            delete m_vecUniformSamplers[i];
            m_vecUniformSamplers[i] = nullptr;
        }

        delete m_MaterialLocalUniformBlockInfo;
        delete m_CommonUniformBlockInfo;
        delete m_MaterialUniformBlockInfo;
        delete m_EngineLocalUniformBlockInfo;
        m_vecUniformBlocks.clear();
        m_vecUniformTextures.clear();
        m_vecUniformSamplers.clear();
    }

    bool RayTracingShaderReflector::BuildReflection(void* pProgram, ShaderStageType ShaderType)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        ID3D12LibraryReflection* pReflection = static_cast<ID3D12LibraryReflection*>(pProgram);

        D3D12_LIBRARY_DESC LibraryDesc;
        pReflection->GetDesc(&LibraryDesc);

        for (uint32_t i = 0; i < LibraryDesc.FunctionCount; ++i)
        {
            D3D12_FUNCTION_DESC FuncDesc;
            ID3D12FunctionReflection* pFunctionReflection = pReflection->GetFunctionByIndex(i);
            assert(pFunctionReflection);

            hrRetCode = pFunctionReflection->GetDesc(&FuncDesc);
            assert(hrRetCode == S_OK);

            m_BindingType = RayTracingBindingType::Global;
            m_BindingSpace = RAYTRACING_GLOBALBINDINGSPACE;
            if ((uint32_t)(ShaderType & ShaderStageType::HitGroup) != 0)
            {
                m_BindingType = RayTracingBindingType::Local;
                m_BindingSpace = RAYTRACING_LOCALBINDINGSPACE;
            }

            bRetCode = ParseCBuffer(pFunctionReflection, FuncDesc);
            KGLOG_ASSERT_EXIT(bRetCode);

            if (m_BindingSpace != RAYTRACING_LOCALL_BINDING_SPACE)
            {
                bRetCode = ParseTexture(pFunctionReflection, FuncDesc);
                KGLOG_ASSERT_EXIT(bRetCode);

                bRetCode = ParseUAV(pFunctionReflection, FuncDesc);
                KGLOG_ASSERT_EXIT(bRetCode);

                bRetCode = ParseSampler(pFunctionReflection, FuncDesc);
                KGLOG_ASSERT_EXIT(bRetCode);
            }
        }
        CombineResourceToIndex();

        bResult = TRUE;
    Exit0:
    return bResult;
    }

    bool RayTracingShaderReflector::ParseCBuffer(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        uint32_t CBVCount = 0;

        for (uint32_t i = 0; i < FuncDesc.ConstantBuffers; ++i)
        {
            ID3D12ShaderReflectionConstantBuffer* pCBuffer = pFuncReflection->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC CBufferDesc = {};
            hrRetCode = pCBuffer->GetDesc(&CBufferDesc);
            ShaderOffset BindingOffset = {};
            assert(hrRetCode == S_OK);

            if (CBufferDesc.Type != D3D_CT_CBUFFER)
            {
                continue;
            }

            D3D12_SHADER_INPUT_BIND_DESC BindDesc;
            for (uint32_t k = 0; k < FuncDesc.BoundResources; k++)
            {
                hrRetCode = pFuncReflection->GetResourceBindingDesc(k, &BindDesc);
                assert(hrRetCode == S_OK);

                if (strcmp(BindDesc.Name, CBufferDesc.Name) == 0)
                {
                    BindingOffset = ShaderOffset{ static_cast<uint16_t>(BindDesc.Space), static_cast<uint16_t>(BindDesc.BindPoint) };
                    break;
                }
            }

            if (BindDesc.Space != m_BindingSpace)
                continue;

            KGFX_ShaderUniformBlockDX12* pUniformBlock = GetUniformBlock(CBufferDesc.Name);
            if (pUniformBlock == nullptr)
            {
                bool bMaterialBindlessIndexUBO = false;
                bool bCommonUBO = false;
                bool bMaterialUBO = false;
                bool bEngineBindlessIndexUBO = false;

                pUniformBlock = new KGFX_ShaderUniformBlockDX12;
                pUniformBlock->m_szName = GetParamNameByPool(CBufferDesc.Name);
                pUniformBlock->m_block16bytesAlignMemoryForGpu = CBufferDesc.Size;
                pUniformBlock->m_uArrayCount = 1;
                pUniformBlock->m_UniformType = enumUniformType::UBO_UNIFORM;

                if (strcmp(pUniformBlock->m_szName, RAYTRACING_COMMON_PARAM_BINDLESS_CB_NAME) == 0)
                {
                    bCommonUBO = true;
                    m_CommonBindlessIndexConstantBufferSize = CBufferDesc.Size;
                }
                else if (strcmp(pUniformBlock->m_szName, RAYTRACING_LOCAL_MATERIAL_BINDLESS_CB_NAME) == 0)
                {
                    bMaterialBindlessIndexUBO = true;
                    m_MaterialBindlessIndexConstantBufferSize = CBufferDesc.Size;
                }
                else if (strcmp(pUniformBlock->m_szName, RAYTRACING_LOCAL_MATERIAL_PARAM_CB_NAME) == 0)
                {
                    bMaterialUBO = true;
                    m_MaterialConstantBufferSize = CBufferDesc.Size;
                }
                else if (strcmp(pUniformBlock->m_szName, RAYTRACING_LOCAL_ENGINE_BINDLESS_CB_NAME) == 0)
                {
                    bEngineBindlessIndexUBO = true;
                    m_EngineBindlessIndexConstantBufferSize = CBufferDesc.Size;
                }

                if (bMaterialBindlessIndexUBO && m_MaterialLocalUniformBlockInfo == nullptr)
                {
                    m_MaterialLocalUniformBlockInfo = new KRayTracingUniformBlockInfo;
                }

                if (bCommonUBO && m_CommonUniformBlockInfo == nullptr)
                {
                    m_CommonUniformBlockInfo = new KRayTracingUniformBlockInfo;
                }

                if (bMaterialUBO && m_MaterialUniformBlockInfo == nullptr)
                {
                    m_MaterialUniformBlockInfo = new KRayTracingUniformBlockInfo;
                }

                if (bEngineBindlessIndexUBO && m_EngineLocalUniformBlockInfo == nullptr)
                {
                    m_EngineLocalUniformBlockInfo = new KRayTracingUniformBlockInfo;
                }

                if (m_EngineLocalUniformBlockInfo || m_MaterialUniformBlockInfo || m_CommonUniformBlockInfo || m_MaterialLocalUniformBlockInfo)
                {
                    for (uint32_t j = 0; j < CBufferDesc.Variables; ++j)
                    {
                        bool bAttached = false;
                        ID3D12ShaderReflectionVariable* pVariable = pCBuffer->GetVariableByIndex(j);
                        D3D12_SHADER_VARIABLE_DESC VariableDesc;
                        D3D12_SHADER_TYPE_DESC VariableTypeDesc;
                        ID3D12ShaderReflectionType* pType = nullptr;
                        ParamItem Item = {};
                        KRayTracingUniformInfo* UniformInfo = new KRayTracingUniformInfo;
                        pType = pVariable->GetType();
                        assert(pType != nullptr);

                        pVariable->GetDesc(&VariableDesc);
                        pType->GetDesc(&VariableTypeDesc);

                        const_pool_str Name = GetParamNameByPool(VariableDesc.Name);
                        Item.Offset = VariableDesc.StartOffset;
                        Item.Size = VariableDesc.Size;

                        UniformInfo->m_nOffset = VariableDesc.StartOffset;
                        UniformInfo->m_szBlockName = pUniformBlock->m_szName;
                        UniformInfo->m_szName = Name;
                        UniformInfo->m_uArrayCount = VariableTypeDesc.Elements;
                        UniformInfo->m_uByteSize = VariableDesc.Size;
                        UniformInfo->m_uMatcol = VariableTypeDesc.Columns;
                        UniformInfo->m_uMatrow = VariableTypeDesc.Rows;

                        if (bMaterialBindlessIndexUBO)
                        {
                            m_mapMaterialBindlessIndexConstant[Name] = Item;
                            m_MaterialLocalUniformBlockInfo->m_mapUnifroms[Name] = UniformInfo;
                        }
                        else if (bCommonUBO)
                        {
                            m_mapCommonBindlessIndexConstant[Name] = Item;
                            m_CommonUniformBlockInfo->m_mapUnifroms[Name] = UniformInfo;

                        }
                        else if (bMaterialUBO)
                        {
                            m_mapMaterialConstant[Name] = Item;
                            m_MaterialUniformBlockInfo->m_mapUnifroms[Name] = UniformInfo;

                        }
                        else if (bEngineBindlessIndexUBO)
                        {
                            m_mapEngineBindlessIndexConstant[Name] = Item;
                            m_EngineLocalUniformBlockInfo->m_mapUnifroms[Name] = UniformInfo;

                        }
                    }
                }

               
            }
            pUniformBlock->m_CBufBinds = BindingOffset;
            m_vecUniformBlocks.push_back(pUniformBlock);

            if (strcmp(BindDesc.Name, RAYTRACING_CUSTOMDATA_NAME) != 0)
            {
                CBVCount++;
            }

            if (strcmp(BindDesc.Name, RAYTRACING_COMMON_PARAM_BINDLESS_CB_NAME) == 0)
            {
                m_CommonUniformBlockInfo->m_nBinding = BindDesc.BindPoint;
                m_CommonUniformBlockInfo->m_nSpace = BindDesc.Space;
                m_CommonUniformBlockInfo->m_szName = RAYTRACING_COMMON_PARAM_BINDLESS_CB_NAME;
                m_CommonUniformBlockInfo->m_UniformType = enumUniformType::UBO_UNIFORM;
                m_CommonUniformBlockInfo->m_block16bytesAlignMemoryForGpu = m_CommonBindlessIndexConstantBufferSize;
            }
            else if (strcmp(BindDesc.Name, RAYTRACING_LOCAL_MATERIAL_BINDLESS_CB_NAME) == 0)
            {
                m_MaterialLocalUniformBlockInfo->m_nBinding = BindDesc.BindPoint;
                m_MaterialLocalUniformBlockInfo->m_nSpace = BindDesc.Space;
                m_MaterialLocalUniformBlockInfo->m_szName = RAYTRACING_LOCAL_MATERIAL_BINDLESS_CB_NAME;
                m_MaterialLocalUniformBlockInfo->m_UniformType = enumUniformType::UBO_UNIFORM;
                m_MaterialLocalUniformBlockInfo->m_block16bytesAlignMemoryForGpu = m_MaterialBindlessIndexConstantBufferSize;
            }
            else if (strcmp(BindDesc.Name, RAYTRACING_LOCAL_MATERIAL_PARAM_CB_NAME) == 0)
            {
                m_MaterialUniformBlockInfo->m_nBinding = BindDesc.BindPoint;
                m_MaterialUniformBlockInfo->m_nSpace = BindDesc.Space;
                m_MaterialUniformBlockInfo->m_szName = RAYTRACING_LOCAL_MATERIAL_PARAM_CB_NAME;
                m_MaterialUniformBlockInfo->m_UniformType = enumUniformType::UBO_UNIFORM;
                m_MaterialUniformBlockInfo->m_block16bytesAlignMemoryForGpu = m_MaterialConstantBufferSize;
            }
            else if (strcmp(BindDesc.Name, RAYTRACING_LOCAL_ENGINE_BINDLESS_CB_NAME) == 0)
            {
                m_EngineLocalUniformBlockInfo->m_nBinding = BindDesc.BindPoint;
                m_EngineLocalUniformBlockInfo->m_nSpace = BindDesc.Space;
                m_EngineLocalUniformBlockInfo->m_szName = RAYTRACING_LOCAL_ENGINE_BINDLESS_CB_NAME;
                m_EngineLocalUniformBlockInfo->m_UniformType = enumUniformType::UBO_UNIFORM;
                m_EngineLocalUniformBlockInfo->m_block16bytesAlignMemoryForGpu = m_EngineBindlessIndexConstantBufferSize;
            }          
        }

        m_Bindings.ConstanBuffers += CBVCount;

        bResult = TRUE;

        return bResult;
    }

    bool RayTracingShaderReflector::ParseTexture(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        uint32_t SRVCount = 0;
        uint32_t SRVStartSlot = UINT32_MAX;

        for (uint32_t i = 0; i < FuncDesc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC BindDesc;
            hrRetCode = pFuncReflection->GetResourceBindingDesc(i, &BindDesc);
            assert(hrRetCode == S_OK);

            if (BindDesc.Type == D3D_SIT_TEXTURE ||
                BindDesc.Type == D3D_SIT_TBUFFER ||
                BindDesc.Type == D3D_SIT_BYTEADDRESS ||
                BindDesc.Type == D3D_SIT_STRUCTURED)
            {
                KGFX_ShaderUniformTextureDX12* pResource = GetUniformTextures(BindDesc.Name);
                if (pResource == nullptr)
                {
                    pResource = new KGFX_ShaderUniformTextureDX12;
                    pResource->m_szName = GetParamNameByPool(BindDesc.Name);
                    pResource->m_UniformType = enumUniformType::TEXTURE_UNIFORM;
                    pResource->m_uArrayCount = BindDesc.BindCount;

                    if (BindDesc.Space == m_BindingSpace)
                    {
                        m_vecUniformTextures.push_back(pResource);
                        SRVCount++;
                    }
                }

                pResource->m_TextureBinds = ShaderOffset{ static_cast<uint16_t>(BindDesc.Space), static_cast<uint16_t>(BindDesc.BindPoint) };
                SRVStartSlot = std::min(SRVStartSlot, BindDesc.BindPoint);
            }
        }


        m_Bindings.ShaderResourceViews += SRVCount;
        m_Bindings.ShaderResourceStartIndex = std::min(SRVStartSlot, m_Bindings.ShaderResourceStartIndex);
        
        bResult = TRUE;

        return bResult;
    }

    bool RayTracingShaderReflector::ParseUAV(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        uint32_t UAVCount = 0;
        uint32_t UAVStartSlot = UINT32_MAX;

        for (uint32_t i = 0; i < FuncDesc.BoundResources; i++)
        {
            D3D12_SHADER_INPUT_BIND_DESC BindDesc;
            hrRetCode = pFuncReflection->GetResourceBindingDesc(i, &BindDesc);
            assert(hrRetCode == S_OK);

            if (BindDesc.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                BindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                BindDesc.Type == D3D_SIT_UAV_RWTYPED ||
                BindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS ||
                BindDesc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                BindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER ||
                BindDesc.Type == D3D_SIT_UAV_FEEDBACKTEXTURE)
            {
                KGFX_ShaderUniformBlockDX12* pUniformBlock = GetUniformBlock(BindDesc.Name);
                if (pUniformBlock == nullptr)
                {
                    pUniformBlock = new KGFX_ShaderUniformBlockDX12;
                    pUniformBlock->m_szName = GetParamNameByPool(BindDesc.Name);
                    pUniformBlock->m_UniformType = SSBO_UNIFORM;
                    pUniformBlock->m_uArrayCount = BindDesc.BindCount;

                    if (BindDesc.Space == m_BindingSpace)
                    {
                        m_vecUniformBlocks.push_back(pUniformBlock);
                        UAVCount++;
                    }
                }

                pUniformBlock->m_CBufBinds = ShaderOffset{ static_cast<uint16_t>(BindDesc.Space), static_cast<uint16_t>(BindDesc.BindPoint) };
                UAVStartSlot = std::min(UAVStartSlot, BindDesc.BindPoint);
            }
        }

        m_Bindings.UnorderedResourceViews += UAVCount;
        m_Bindings.UnorderedResourceStartIndex = std::min(UAVStartSlot, m_Bindings.UnorderedResourceStartIndex);

        bResult = TRUE;

        return bResult;
    }

    bool RayTracingShaderReflector::ParseSampler(ID3D12FunctionReflection* pFuncReflection, const D3D12_FUNCTION_DESC& FuncDesc)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        uint32_t SamplerCount = 0;
        uint32_t SamplerStartSlot = UINT32_MAX;

        for (uint32_t i = 0; i < FuncDesc.BoundResources; i++)
        {
            D3D12_SHADER_INPUT_BIND_DESC BindDesc;
            hrRetCode = pFuncReflection->GetResourceBindingDesc(i, &BindDesc);
            assert(hrRetCode == S_OK);

            if (BindDesc.Type == D3D_SIT_SAMPLER)
            {
                KGFX_ShaderUniformSamplerDX12* pSampler = GetUniformSamplers(BindDesc.Name);
                if (pSampler == nullptr)
                {
                    pSampler = new KGFX_ShaderUniformSamplerDX12;
                    pSampler->m_szName = GetParamNameByPool(BindDesc.Name);

                    if (BindDesc.Space == m_BindingSpace)
                    {
                        m_vecUniformSamplers.push_back(pSampler);
                        SamplerCount++;
                    }
                }

                pSampler->m_SamplerBinds = ShaderOffset{ static_cast<uint16_t>(BindDesc.Space), static_cast<uint16_t>(BindDesc.BindPoint)};
                SamplerStartSlot = std::min(SamplerStartSlot, BindDesc.BindPoint);
            }
        }

        m_Bindings.Samplers += SamplerCount;
        m_Bindings.SamplersStartIndex = std::min(SamplerStartSlot, m_Bindings.SamplersStartIndex);

        bResult = TRUE;

        return bResult;
    }

    void RayTracingShaderReflector::CombineResourceToIndex()
    {
        if (m_Bindings.ConstanBuffers > 0)
        {
            for (auto Resource : m_vecUniformBlocks)
            {
                if (Resource->m_UniformType == enumUniformType::UBO_UNIFORM)
                {
                    m_mapResourceNameToIndex[Resource->m_szName] = Resource->m_CBufBinds.bindingSlotIndex;
                }
            }
        }

        if (m_Bindings.ShaderResourceViews > 0)
        {
            assert(m_Bindings.ShaderResourceStartIndex == 0);

            for (auto Resource : m_vecUniformTextures)
            {
                m_mapResourceNameToIndex[Resource->m_szName] = Resource->m_TextureBinds.bindingSlotIndex;
            }
        }

        if (m_Bindings.UnorderedResourceViews > 0)
        {
            assert(m_Bindings.UnorderedResourceStartIndex == 0);
            uint32_t IndexOffset = m_Bindings.ShaderResourceViews;

            for (auto Resource : m_vecUniformBlocks)
            {
                if (Resource->m_UniformType == enumUniformType::SSBO_UNIFORM)
                {
                    m_mapResourceNameToIndex[Resource->m_szName] = Resource->m_CBufBinds.bindingSlotIndex + IndexOffset;
                }
            }
        }
    }

    KGFX_ShaderUniformBlockDX12* RayTracingShaderReflector::GetUniformBlock(const_pool_str Name)
    {
        for (auto pUniformBlock : m_vecUniformBlocks)
        {
            if (strcmp(pUniformBlock->m_szName, Name) == 0)
            {
                return pUniformBlock;
            }
        }

        return nullptr;
    }

    KGFX_ShaderUniformTextureDX12* RayTracingShaderReflector::GetUniformTextures(const_pool_str Name)
    {
        for (auto pUniformTexture : m_vecUniformTextures)
        {
            if (strcmp(pUniformTexture->m_szName, Name) == 0)
            {
                return pUniformTexture;
            }
        }

        return nullptr;
    }

    KGFX_ShaderUniformSamplerDX12* RayTracingShaderReflector::GetUniformSamplers(const_pool_str Name)
    {
        for (auto pSampler : m_vecUniformSamplers)
        {
            if (strcmp(pSampler->m_szName, Name) == 0)
            {
                return pSampler;
            }
        }

        return nullptr;
    }

    RayTracingShaderDx12::~RayTracingShaderDx12()
    {
        SAFE_DELETE(m_pReflector);
        SAFE_DELETE(m_pRootSigature);
    }

    bool RayTracingShaderDx12::Create(const KRayTracingShaderCreateDesc& ShaderCreateDesc)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        ShaderStageType StageType;
        NSKBase::tagFileLocation FileLocation{};
        assert(!ShaderCreateDesc.vecSubmodule.empty());
        
        for (uint32_t i = 0; i < ShaderCreateDesc.vecSubmodule.size(); ++i)
        {
            if (ShaderCreateDesc.vecSubmodule[i].sType == ERTShaderSubType::E_RT_TYPE_CLOSEST_HIT)
            {
                m_EntryPoint = CharToWChar(ShaderCreateDesc.vecSubmodule[i].szEntryPoint);
            }
            else if (ShaderCreateDesc.vecSubmodule[i].sType == ERTShaderSubType::E_RT_TYPE_ANY_HIT)
            {
                m_AnyHitEntryPoint = CharToWChar(ShaderCreateDesc.vecSubmodule[i].szEntryPoint);
            }
            else
            {
                m_IntersectionEntryPoint = CharToWChar(ShaderCreateDesc.vecSubmodule[i].szEntryPoint);
            }
        }

        StageType = ConvertShaderStage(ShaderCreateDesc.sType);
        if (ShaderCreateDesc.vecSubmodule[0].userShaderFileLoc)
        {
            FileLocation = *(ShaderCreateDesc.vecSubmodule[0].userShaderFileLoc);
        }
        bRetCode = LoadRayTracingShader(StageType, ShaderCreateDesc.vecSubmodule[0].szMainShaderPath, FileLocation, ShaderCreateDesc.vecSubmodule[0].szMacroDefine, nullptr);
        KGLOG_ASSERT_EXIT(bRetCode);

        if (m_pRootSigature == nullptr)
        {
            m_pRootSigature = new RayTracingRootSignature;
            RayTracingSignatureDesc RootSignatureDesc;
            RootSignatureDesc.Binding = m_pReflector->m_Bindings;
            RootSignatureDesc.Type = m_pReflector->m_BindingType;
            bRetCode = m_pRootSigature->Init(RootSignatureDesc);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        m_Hash = ShaderCreateDesc.inHash;
        m_ShaderType = ShaderCreateDesc.sType;

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    KRayTracingUniformBlockInfo* RayTracingShaderDx12::GetLocalMaterialBindlessIDUniformBlockInfo()
    {
        return m_pReflector->m_MaterialLocalUniformBlockInfo;
    }

    KRayTracingUniformBlockInfo* RayTracingShaderDx12::GetCommonUniformBlockInfo()
    {
        return m_pReflector->m_CommonUniformBlockInfo;
    }

    KRayTracingUniformBlockInfo* RayTracingShaderDx12::GetLocalMaterialParamUniformBlockInfo()
    {
        return m_pReflector->m_MaterialUniformBlockInfo;
    }

    KRayTracingUniformBlockInfo* RayTracingShaderDx12::GetLocalEngineBindlessIDUniformBlockInfo()
    {
        return m_pReflector->m_EngineLocalUniformBlockInfo;
    }

    std::string GetShaderKey(ShaderStageType eShaderStage, const char* szShaderFilePath, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro, const char* szFileDefMacro)
    {
        std::string Key = szShaderFilePath;
        auto        szShaderTypeName = IncludeFileHelper::GetShaderTypeName(eShaderStage);
        Key.append("@");

        Key.append(szShaderTypeName);

        if (sUserShaderLoc.IsValid())
        {
            Key.append("@");
            Key.append(sUserShaderLoc.GetFilePath().Str());
        }

        if (szUserDefMacro && szUserDefMacro[0])
        {
            Key.append("@");
            Key.append(szUserDefMacro);
        }

        if (szFileDefMacro && szFileDefMacro[0])
        {
            Key.append("@");
            Key.append(szFileDefMacro);
        }
        return Key;
    }

    bool RayTracingShaderDx12::LoadRayTracingShader(ShaderStageType eShaderStage, const char* szShaderPath, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro, const char* szFileDefMacro)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;

        KGFX_ShaderTechItemDX12 ShaderTechItem = {};
        ShaderTechItem.m_EntryPoint = "";
        ShaderTechItem.m_MainShaderPath = szShaderPath;
        ShaderTechItem.m_ShageStage = eShaderStage;
        ShaderTechItem.m_Key = GetShaderKey(eShaderStage, szShaderPath, sUserShaderLoc, szUserDefMacro, szFileDefMacro);

        if (sUserShaderLoc.GetFilePath().IsValid())
        {
            ShaderTechItem.m_UserShaderPath = sUserShaderLoc.GetFilePathStr();
        }

        if (szUserDefMacro && szUserDefMacro[0])
        {
            IncludeFileHelper::ExpandMacoDefineDXC(szUserDefMacro, ShaderTechItem.m_TechMacroDXC);
        }

        if (szFileDefMacro && szFileDefMacro[0])
        {
            IncludeFileHelper::ExpandMacoDefineDXC(szFileDefMacro, ShaderTechItem.m_TechMacroDXC);
        }

        ShaderTechItem.LoadShader();

        bRetCode = CompileDXC(&ShaderTechItem);
        KGLOG_ASSERT_EXIT(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    bool RayTracingShaderDx12::CompileDXC(KGFX_ShaderTechItemDX12* pShaderTechItem)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        auto DxcComp = KGFX_GetDXCComplier();
        ID3D12LibraryReflection* pReflection = nullptr;
        std::string ErrorMsg = {};
        std::vector<std::string> vecErrors, vecWarnings, vecNotes, vecDebugInfos;
        std::regex ErrorRegex(R"((.*error:.*))");
        std::regex WariningRegex(R"((.*warning:.*))");
        std::regex NoteRegex(R"((.note:.*))");
        std::smatch Match;
        std::istringstream Iss;
        std::string Line;

        IDxcBlob* pBlob = nullptr;
        bRetCode = DxcComp->CheckLibShaderCache(pShaderTechItem, &pReflection, &pBlob, ErrorMsg);
        KGLOG_ASSERT_EXIT(bRetCode);

        Iss.str(ErrorMsg);
        while (std::getline(Iss, Line))
        {
            if (std::regex_search(Line, Match, ErrorRegex))
            {
                vecErrors.push_back(Line);
            }
            else if (std::regex_search(Line, Match, WariningRegex))
            {
                vecWarnings.push_back(Line);
            }
            else if (std::regex_search(Line, Match, NoteRegex))
            {
                vecNotes.push_back(Line);
            }
            else
            {
                vecDebugInfos.push_back(Line);
            }
        }

        for (auto& Msg : vecErrors)
        {
            KGLogPrintf(KGLOG_ERR, Msg.data());
        }

        for (auto& Msg : vecWarnings)
        {
            KGLogPrintf(KGLOG_WARNING, Msg.data());
        }

        for (auto& Msg : vecNotes)
        {
            KGLogPrintf(KGLOG_INFO, Msg.data());
        }

        for (auto& Msg : vecDebugInfos)
        {
            KGLogPrintf(KGLOG_DEBUG, Msg.data());
        }

        KGLOG_PROCESS_ERROR(pReflection);

        m_pCompiledShader = reinterpret_cast<ID3DBlob*>(pBlob);
        m_HashCode = Fnv1a64(m_pCompiledShader->GetBufferPointer(), m_pCompiledShader->GetBufferSize());
        if (m_pReflector == nullptr)
        {
            m_pReflector = new RayTracingShaderReflector;
        }

        bRetCode = m_pReflector->BuildReflection(pReflection, pShaderTechItem->m_ShageStage);
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = TRUE;
    Exit0:
        SAFE_RELEASE(pReflection);
        return bResult;
    }

    enumRayTracingShaderType RayTracingShaderDx12::GetType()const
    {
        return m_ShaderType;
    }

    uint64_t RayTracingShaderDx12::GetHash()
    {
        return m_Hash;
    }

    bool RayTracingPipelineStateDx12::Create(const RayTracingProgramDesc& Initializer)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        HRESULT hrRetCode = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        uint32_t MaxShaderCount = (uint32_t)(Initializer.vecRayGenShaders.size() + Initializer.vecMissShaders.size() + Initializer.vecHitShaders.size() + Initializer.vecCallableShaders.size());
        ID3D12RootSignature* pGlobalRootSignature = nullptr;
        RayTracingPipelineCacheDx12* pPipelineCache = nullptr;
        const std::vector< IRayTracingShader*>& vecRayGenShaders = Initializer.vecRayGenShaders;
        const std::vector< IRayTracingShader*>& vecMissShaders = Initializer.vecMissShaders;
        const std::vector< IRayTracingShader*>& vecHitGroupShaders = Initializer.vecHitShaders;
        const std::vector< IRayTracingShader*>& vecCallableShaders = Initializer.vecCallableShaders;
        std::vector< D3D12_EXISTING_COLLECTION_DESC> vecExistingCollections;
        std::vector< RayTracingPipelineCacheDx12::Entry*> vecRayGenCaches;
        std::vector< RayTracingPipelineCacheDx12::Entry*> vecMissCaches;
        std::vector< RayTracingPipelineCacheDx12::Entry*> vecHitGroupCaches;
        std::vector< RayTracingPipelineCacheDx12::Entry*> vecCallableCaches;
        std::vector<DXILLibrary> vecDxilLibrarys;
        std::vector< LPCWSTR> vecExportNames;
        std::vector<D3D12_HIT_GROUP_DESC> vecHitGroups;
        std::vector< ID3D12RootSignature*> vecLocalRootSignatures;
        std::vector<uint32_t> vecLocalRootSignatureAssociations;
        uint32_t MaxAttributeSizeInBytes = 0;
        uint32_t MaxPayloadSizeInBytes = 0;
        auto ShaderIdentifierFun = [this](RayTracingPipelineCacheDx12::Entry* pInEntry)->RayTracingShaderIdentifier
        {
            if (pInEntry->ShaderIdentifier.IsValid())
            {
                return pInEntry->ShaderIdentifier;
            }
            else
            {
                return GetShaderIdentifier(m_pPipelineProperties, pInEntry->GetPrimaryExportName());
            }
        };
        Destroy();



        nRetCode = KRayTracingProgram::Create(Initializer);
        KGLOG_ASSERT_EXIT(nRetCode);

        pGlobalRootSignature = pGraphicDevice->GetRayTracingGlobalRootSignature()->GetDeviceRootSignature();
        pPipelineCache = pGraphicDevice->GetRayTracingPipelineCache();

        //m_RayGenShaders.vecRayTracingShaders.resize(vecRayGenShaders.size());
        m_RayGenShaders.vecRayTracingShaderIdentifiers.resize(vecRayGenShaders.size());
        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecRayGenShaders.size(); ++ShaderIndex)
        {
            RayTracingPipelineCacheDx12::Entry* pEntry = pPipelineCache->GetOrCompileShaderToStateObject((RayTracingShaderDx12*)vecRayGenShaders[ShaderIndex], pGlobalRootSignature, MaxAttributeSizeInBytes, MaxPayloadSizeInBytes, RayTracingPipelineCacheDx12::CollectionType::RayGen);
            KGLOG_ASSERT_EXIT(pEntry);
            vecRayGenCaches.emplace_back(pEntry);
            m_RayGenShaders.vecRayTracingShaders.emplace_back((RayTracingShaderDx12*)vecRayGenShaders[ShaderIndex]);
            vecExistingCollections.emplace_back(pEntry->GetCollectionDesc());
        }

        m_MissShaders.vecRayTracingShaderIdentifiers.resize(vecMissShaders.size());
        //m_MissShaders.vecRayTracingShaders.resize(vecMissShaders.size());
        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecMissShaders.size(); ++ShaderIndex)
        {
            RayTracingPipelineCacheDx12::Entry* pEntry = pPipelineCache->GetOrCompileShaderToStateObject((RayTracingShaderDx12*)vecMissShaders[ShaderIndex], pGlobalRootSignature, MaxAttributeSizeInBytes, MaxPayloadSizeInBytes, RayTracingPipelineCacheDx12::CollectionType::Miss);
            KGLOG_ASSERT_EXIT(pEntry);
            vecMissCaches.emplace_back(pEntry);
            m_MissShaders.vecRayTracingShaders.emplace_back((RayTracingShaderDx12*)vecMissShaders[ShaderIndex]);
            vecExistingCollections.emplace_back(pEntry->GetCollectionDesc());
        }

        m_HitGroupShaders.vecRayTracingShaderIdentifiers.resize(vecHitGroupShaders.size());
        //m_HitGroupShaders.vecRayTracingShaders.resize(vecHitGroupShaders.size());
        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecHitGroupShaders.size(); ++ShaderIndex)
        {
            RayTracingPipelineCacheDx12::Entry* pEntry = pPipelineCache->GetOrCompileShaderToStateObject((RayTracingShaderDx12*)vecHitGroupShaders[ShaderIndex], pGlobalRootSignature, MaxAttributeSizeInBytes, MaxPayloadSizeInBytes, RayTracingPipelineCacheDx12::CollectionType::HitGroup);
            KGLOG_ASSERT_EXIT(pEntry);
            vecHitGroupCaches.emplace_back(pEntry);
            m_HitGroupShaders.vecRayTracingShaders.emplace_back((RayTracingShaderDx12*)vecHitGroupShaders[ShaderIndex]);
            vecExistingCollections.emplace_back(pEntry->GetCollectionDesc());
        }

        m_CallableShaders.vecRayTracingShaderIdentifiers.resize(vecCallableShaders.size());
        //m_CallableShaders.vecRayTracingShaders.resize(vecCallableShaders.size());
        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecCallableShaders.size(); ++ShaderIndex)
        {
            RayTracingPipelineCacheDx12::Entry* pEntry = pPipelineCache->GetOrCompileShaderToStateObject((RayTracingShaderDx12*)vecCallableShaders[ShaderIndex], pGlobalRootSignature, MaxAttributeSizeInBytes, MaxPayloadSizeInBytes, RayTracingPipelineCacheDx12::CollectionType::Callable);
            KGLOG_ASSERT_EXIT(pEntry);
            vecCallableCaches.emplace_back(pEntry);
            m_CallableShaders.vecRayTracingShaders.emplace_back((RayTracingShaderDx12*)vecCallableShaders[ShaderIndex]);
            vecExistingCollections.emplace_back(pEntry->GetCollectionDesc());
        }

        m_pStateObject = CreateRayTracingStateObject(
            vecDxilLibrarys,
            vecExportNames,
            vecHitGroups,
            vecLocalRootSignatures,
            vecLocalRootSignatureAssociations,
            vecExistingCollections,
            pGlobalRootSignature,
            MaxAttributeSizeInBytes,
            MaxPayloadSizeInBytes,
            D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE
        );

        KGLOG_ASSERT_EXIT(m_pStateObject != nullptr);

        hrRetCode = m_pStateObject->QueryInterface(IID_PPV_ARGS(&m_pPipelineProperties));
        KGLOG_COM_ASSERT_EXIT(hrRetCode);

        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecRayGenCaches.size(); ++ShaderIndex)
        {
            m_RayGenShaders.vecRayTracingShaderIdentifiers[ShaderIndex] = ShaderIdentifierFun(vecRayGenCaches[ShaderIndex]);
        }

        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecMissCaches.size(); ++ShaderIndex)
        {
            m_MissShaders.vecRayTracingShaderIdentifiers[ShaderIndex] = ShaderIdentifierFun(vecMissCaches[ShaderIndex]);
        }

        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecHitGroupCaches.size(); ++ShaderIndex)
        {
            m_HitGroupShaders.vecRayTracingShaderIdentifiers[ShaderIndex] = ShaderIdentifierFun(vecHitGroupCaches[ShaderIndex]);
        }

        for (uint32_t ShaderIndex = 0; ShaderIndex < (uint32_t)vecCallableCaches.size(); ++ShaderIndex)
        {
            m_CallableShaders.vecRayTracingShaderIdentifiers[ShaderIndex] = ShaderIdentifierFun(vecCallableCaches[ShaderIndex]);
        }

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    bool RayTracingPipelineStateDx12::BeginBind(IKGFX_RenderContext* pRenderCtx)
    {
        bool bResult = FALSE;
        bool bRetCode = FALSE;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        m_vecBindlessBindInfos.clear();
        m_vecBufferBindViews.clear();
        m_vecBindInfos.clear();

        uint32_t ResourceCount = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_Bindings.ShaderResourceViews + m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_Bindings.UnorderedResourceViews;
        m_vecBufferBindViews.resize(m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_Bindings.ConstanBuffers);
        m_vecBindInfos.resize(ResourceCount);

        bResult = TRUE;
        return bResult;
    }

    template<typename View>
    void RayTracingPipelineStateDx12::BindResource(const_pool_str pcszName, View* pView)
    {
        RayTracingShaderDx12* pRayGenShader = m_RayGenShaders.vecRayTracingShaders[0];
        auto Iter = pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.find(pcszName);
        if (Iter != pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.end())
        {
            m_vecBindInfos.push_back(pView);
        }
    }

    template<typename View>
    void RayTracingPipelineStateDx12::BindResource(const_pool_str pcszName, uint32_t Num, View** ppViews)
    {
        RayTracingShaderDx12* pRayGenShader = m_RayGenShaders.vecRayTracingShaders[0];
        auto Iter = pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.find(pcszName);
        if (Iter != pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.end())
        {
            m_vecBindInfos.push_back({ ppViews, Num });
        }
    }

    /*bool RayTracingPipelineStateDx12::BindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, pBufView);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufferUAV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, pBufferUAV);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindUAV(const_pool_str pcszName, IKGFX_TextureView* pTextureUAV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, pTextureUAV);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufferSRV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, pBufferSRV);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindSRV(const_pool_str pcszName, IKGFX_TextureView* pTextureSRV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, pTextureSRV);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindUAVArray(const_pool_str pcszName, uint32_t Num, IKGFX_BufferView** ppBufferViews)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, Num, ppBufferViews);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindUAVArray(const_pool_str pcszName, uint32_t Num, IKGFX_TextureView** ppTextureViews)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, Num, ppTextureViews);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindSRVArray(const_pool_str pcszName, uint32_t Num, IKGFX_BufferView** ppBufferViews)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, Num, ppBufferViews);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindSRVArray(const_pool_str pcszName, uint32_t Num, IKGFX_TextureView** ppTextureViewss)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        BindResource(pcszName, Num, ppTextureViewss);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::BindSampler(const_pool_str pcszName, IKGFX_Sampler* pSampler)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        RayTracingShaderDx12* pRayGenShader = m_RayGenShaders.vecRayTracingShaders[0];
        auto Iter = pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.find(pcszName);
        if (Iter != pRayGenShader->m_pReflector->m_mapCommonBindlessIndexConstant.end())
        {
            uint32_t BindlessIndex = pSampler->GetBindlessView()->GetBindlessHandle();
            m_vecBindInfos.push_back(pSampler);
        }

        bResult = TRUE;
        return bResult;
    }*/

    bool RayTracingPipelineStateDx12::AddBindlessCBV(IKGFX_BufferView* pBufView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(pBufView);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessUAV(IKGFX_BufferView* pUAV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(pUAV);
        
        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessUAV(IKGFX_TextureView* pTexView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        gfx::IKGFX_RenderContext* pRenderCtx = gfx::GetRenderContext();

        gfx::KGfxBarrier Barrier;
        auto* pResource = pTexView->GetResource();
        Barrier.pTexture = pResource;
        Barrier.eType = gfx::KGfxBarrier::EType::Texture;
        Barrier.eDSTAccess = KGfxAccess::UAVMask;
        Barrier.eSRCAccess = KGfxAccess::Unknown;
        pRenderCtx->Transition(Barrier);

        m_vecBindlessBindInfos.emplace_back(pTexView);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessSRV(IKGFX_BufferView* pSRV)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(pSRV);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessSRV(IKGFX_TextureView* pTexView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(pTexView);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessSampler(IKGFX_Sampler* pSampler)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(pSampler);

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindlessRayTracingScene(KRayTracingScene* pRTScene)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders.size() == 1);

        m_vecBindlessBindInfos.emplace_back(((RayTracingSceneDx12*)pRTScene)->getTopAccelerationStructureBufferView());

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        auto Iter = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.find(pcszName);
        if (Iter != m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.end())
        {
            m_vecBufferBindViews[Iter->second] = pBufView;
        }

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        auto Iter = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.find(pcszName);
        if (Iter != m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.end())
        {
            m_vecBindInfos[Iter->second] = pBufView->GetNativeHandle();
        }

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        auto Iter = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.find(pcszName);
        if (Iter != m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.end())
        {
            m_vecBindInfos[Iter->second] = pTexView->GetNativeHandle();
        }

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pBufView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        auto Iter = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.find(pcszName);
        if (Iter != m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.end())
        {
            m_vecBindInfos[Iter->second] = pBufView->GetNativeHandle();
        }

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView)
    {
        bool bResult = FALSE;
        assert(m_RayGenShaders.vecRayTracingShaders[0] != nullptr);

        auto Iter = m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.find(pcszName);
        if (Iter != m_RayGenShaders.vecRayTracingShaders[0]->m_pReflector->m_mapResourceNameToIndex.end())
        {
            m_vecBindInfos[Iter->second] = pTexView->GetNativeHandle();
        }

        bResult = TRUE;
        return bResult;
    }

    bool RayTracingPipelineStateDx12::EndBind(IKGFX_RenderContext* pRenderCtx)
    {
        bool bResult = FALSE;

        BindToBindless();
        CompairBindInfo();

        bResult = TRUE;
        return bResult;
    }

    void RayTracingPipelineStateDx12::BindToBindless()
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        assert(pBindlessHeapManager != nullptr);

        for (uint32_t i = 0; i < m_vecBindlessBindInfos.size(); i++)
        {
            for (uint32_t j = 0; j < m_vecBindlessBindInfos[i].vecHandles.size(); ++j)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
                BindlessDescriptor Descriptor;

                Descriptor.Index = m_vecBindlessBindInfos[i].vecBindlessIndexs[j];
                Descriptor.Type = m_vecBindlessBindInfos[i].Type;
                CpuHandle.ptr = m_vecBindlessBindInfos[i].vecHandles[j];
                pBindlessHeapManager->CopyDescriptorToBindless(Descriptor, CpuHandle);
            }
        }
    }

    void RayTracingPipelineStateDx12::CompairBindInfo()
    {
        uint64_t Hash = 0;
        BindInfoCache Cache = {};
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        assert(pBindlessHeapManager != nullptr);

        for (uint32_t i = 0; i < m_vecBindInfos.size(); i++)
        {
            Hash += Fnv1a64(&m_vecBindInfos[i], sizeof(m_vecBindInfos[i]));
        }
        Cache.Hash = Hash;
        Cache.ResourceCount = (uint32_t)m_vecBindInfos.size();

        if (m_BindInfoCache != Cache)
        {
            pBindlessHeapManager->Free(BindlessHeapType::Standard, m_BindInfoCache.StarIndex, m_BindInfoCache.ResourceCount);

            uint32_t StartIndex = pBindlessHeapManager->Allocate(BindlessHeapType::Standard, (uint32_t)m_vecBindInfos.size());
            Cache.StarIndex = StartIndex;
            m_BindInfoCache = Cache;
            m_bCacheDirty = true;
        }
    }

    void RayTracingPipelineStateDx12::Apply(IKGFX_RenderContext* pRenderContext, const RayTracingShaderDx12* pRayGenShader, RayTracingShaderBindingTableDx12* pShaderBindingTable)
    {
        std::vector< IKGFX_BufferView*> vecBufferViews;
        std::vector< IKGFX_TextureView*> vecSRVViews;
        std::vector< IKGFX_TextureView*> vecUAVViews;
        std::vector<IKGFX_SamplerBindlessView*> vecSamplerViews;
        std::vector<uint64_t> vecResourceHandles;

        vecBufferViews = m_vecBufferBindViews;
        uint32_t SRVCount = 0;
        uint32_t UAVCount = 0;

        if (m_bCacheDirty)
        {
            vecResourceHandles = m_vecBindInfos;
            SRVCount = pRayGenShader->m_pReflector->m_Bindings.ShaderResourceViews;
            UAVCount = pRayGenShader->m_pReflector->m_Bindings.UnorderedResourceViews;
            m_bCacheDirty = false;
        }

        SetRayTracingGlobalShaderResources(pRenderContext, pRayGenShader, pShaderBindingTable, vecBufferViews.data(), (uint32_t)vecBufferViews.size(),
            vecResourceHandles, SRVCount, UAVCount, m_BindInfoCache.StarIndex, 0, nullptr);
    }

    ID3D12StateObject* RayTracingPipelineStateDx12::GetD3D12StateObject()
    {
        return m_pStateObject;
    }

    RayTracingPipelineStateDx12::~RayTracingPipelineStateDx12()
    {
        Destroy();
    }

    void RayTracingPipelineStateDx12::Destroy()
    {
        SAFE_RELEASE(m_pStateObject);
        SAFE_RELEASE(m_pPipelineProperties);
    }

    RayTracingShaderBindingTableDx12::~RayTracingShaderBindingTableDx12()
    {
        Destroy();
    }

    BOOL RayTracingShaderBindingTableDx12::Create(const ShaderBindingTableDesc& Initializer)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        m_vecShaderBindingTableData.clear();

        m_NumRayGenRecords = Initializer.uInitRayGenCount;
        m_NumHitRecords = Initializer.uInitHitGroupCount;
        m_NumMissRecords = Initializer.uInitMissCount;
        m_NumCallableRecords = Initializer.uInitCallableCount;
        uint32_t MaxLocalBindingDataSize = std::max(Initializer.uCallableRecordAlignedSizeInByte, std::max(Initializer.uHitRecordAlignedSizeInByte, std::max(Initializer.uMissRecordAlignedSizeInByte, Initializer.uRayGenRecordAlignedSizeInByte)));
        //m_LocalRecordStride = RoundUpToNextMultiple(ShaderIdentifierSize + MaxLocalBindingDataSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_LocalRecordStride = RoundUpToNextMultiple(MaxLocalBindingDataSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

        uint32_t TotalRecordSize = 0;
        TotalRecordSize += m_LocalRecordStride;
        //TotalRecordSize = RoundUpToNextMultiple(TotalRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        m_HitGroupShaderTableOffset = TotalRecordSize;
        TotalRecordSize += m_NumHitRecords * m_LocalRecordStride;
        //TotalRecordSize = RoundUpToNextMultiple(TotalRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        m_CallableShaderTableOffset = TotalRecordSize;
        TotalRecordSize += m_NumCallableRecords * m_LocalRecordStride;
        //TotalRecordSize = RoundUpToNextMultiple(TotalRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        m_MissShaderTableOffset = TotalRecordSize;
        TotalRecordSize += m_NumMissRecords * m_LocalRecordStride;
        //TotalRecordSize = RoundUpToNextMultiple(TotalRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

        m_vecShaderBindingTableData.resize(TotalRecordSize);

        nResult = TRUE;
        return nResult;
    }

    bool RayTracingShaderBindingTableDx12::CommitShaderBindingTable(IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        IKGFX_GraphicDevice* pGraphicDevice = KGFX_GetGraphicDevice();
        KGfxBufferDesc BufferDesc{};
        IKGFX_Buffer* m_pBuffer = nullptr;
        gfx::KGfxBarrier ShaderBindingTableBarrier;
        SAFE_RELEASE(m_pShaderBindingTableBuffer);

        BufferDesc.uByteWidth = (uint32_t)m_vecShaderBindingTableData.size();
        BufferDesc.uUsageFlags = gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;

        nRetCode = pGraphicDevice->CreateBuffer(&m_pBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        m_pShaderBindingTableBuffer = m_pBuffer;
        m_pBuffer = nullptr;

        pRenderContext->CmdUpdateSubResource(m_pShaderBindingTableBuffer, 0, (uint32_t)m_vecShaderBindingTableData.size() * sizeof(uint8_t), m_vecShaderBindingTableData.data());

        ShaderBindingTableBarrier.pBuffer = m_pShaderBindingTableBuffer;
        ShaderBindingTableBarrier.eType = gfx::KGfxBarrier::EType::Buffer;
        ShaderBindingTableBarrier.eDSTAccess = KGfxAccess::SRVGraphicsNonPixel;
        ShaderBindingTableBarrier.eSRCAccess = KGfxAccess::Unknown;
        pRenderContext->Transition(ShaderBindingTableBarrier);

        nResult = TRUE;
    Exit0:
        SAFE_RELEASE(m_pBuffer);
        return nResult;
    }

    D3D12_DISPATCH_RAYS_DESC RayTracingShaderBindingTableDx12::GetDispatchDesc()
    {
        D3D12_DISPATCH_RAYS_DESC Desc{};
        D3D12_GPU_VIRTUAL_ADDRESS ShaderTableAddress = m_pShaderBindingTableBuffer->GetBufferDeviceAddress();

        Desc.RayGenerationShaderRecord.StartAddress = ShaderTableAddress;
        Desc.RayGenerationShaderRecord.SizeInBytes = m_LocalRecordStride;

        Desc.MissShaderTable.StartAddress = ShaderTableAddress + m_MissShaderTableOffset;
        Desc.MissShaderTable.SizeInBytes = m_NumMissRecords * m_LocalRecordStride;
        Desc.MissShaderTable.StrideInBytes = m_LocalRecordStride;

        Desc.HitGroupTable.StartAddress = ShaderTableAddress + m_HitGroupShaderTableOffset;
        Desc.HitGroupTable.SizeInBytes = m_NumHitRecords * m_LocalRecordStride;
        Desc.HitGroupTable.StrideInBytes = m_LocalRecordStride;

        if (m_NumCallableRecords)
        {
            Desc.CallableShaderTable.StartAddress = ShaderTableAddress + m_CallableShaderTableOffset;
            Desc.CallableShaderTable.SizeInBytes = m_NumCallableRecords * m_LocalRecordStride;
            Desc.CallableShaderTable.StrideInBytes = m_LocalRecordStride;
        }

        return Desc;
    }

    void RayTracingShaderBindingTableDx12::Destroy()
    {
        SAFE_RELEASE(m_pShaderBindingTableBuffer);
        m_vecShaderBindingTableData.clear();
    }

    bool RayTracingShaderBindingTableDx12::SetShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline, enumRayTracingShaderType sType)
    {
        bool bResult = false;
        bool bRetCode = false;

        switch (sType)
        {
        case KRT_ST_RAY_GEN:
            bRetCode = SetRayGenShaderBinding(binding, pipeline);
            KGLOG_ASSERT_EXIT(bRetCode);
            break;

        case KRT_ST_HIT_GROUP:
            bRetCode = SetHitGroupBinding(binding, pipeline);
            KGLOG_ASSERT_EXIT(bRetCode);
            break;

        case KRT_ST_MISS:
            bRetCode = SetMissShaderBinding(binding, pipeline);
            KGLOG_ASSERT_EXIT(bRetCode);
            break;

        case KRT_ST_CALLABLE:
            bRetCode = SetCallableShaderBinding(binding, pipeline);
            KGLOG_ASSERT_EXIT(bRetCode);
            break;
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    void RayTracingShaderBindingTableDx12::SetShaderBindingTable(const KRayTracingShaderBinding& BindingData, RayTracingShaderIdentifier& Identifier)
    {
        SetShaderIndentifier(BindingData.uRecordIndex, Identifier);
        uint64_t VertexBufferAddresss = BindingData.pLocalVertexBuffer == nullptr ? 0 : BindingData.pLocalVertexBuffer->GetBufferDeviceAddress();
        uint64_t IndexBufferAddresss = BindingData.pLocalIndexBuffer == nullptr ? 0 : BindingData.pLocalIndexBuffer->GetBufferDeviceAddress();
        uint64_t CustomInfoBufferAddresss = BindingData.pLocalCustomInfoCbuffer == nullptr ? 0 : BindingData.pLocalCustomInfoCbuffer->GetBufferDeviceAddress();
        uint64_t LocalEngineBindlessBufferAddresss = BindingData.pLocalEngineBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialParamBufferAddresss = BindingData.pLocalMaterialParamCbuffer == nullptr ? 0 : BindingData.pLocalMaterialParamCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialBindlessBufferAddresss = BindingData.pLocalMaterialBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t data[] = { VertexBufferAddresss, IndexBufferAddresss, CustomInfoBufferAddresss, LocalEngineBindlessBufferAddresss, LocalMaterialParamBufferAddresss, LocalMaterialBindlessBufferAddresss };
        SetBindingData(BindingData.uRecordIndex, ShaderIdentifierSize, data, sizeof(data));
    }

    bool RayTracingShaderBindingTableDx12::SetHitGroupBinding(const KRayTracingShaderBinding& BindingData, KRayTracingProgram* pPipeline)
    {
        RayTracingShaderIdentifier Identifier = ((RayTracingPipelineStateDx12*)pPipeline)->m_HitGroupShaders.vecRayTracingShaderIdentifiers[BindingData.uShaderIndexInPipeline];
        SetHitGroupShaderIdentifier(BindingData.uRecordIndex, Identifier);
        uint64_t VertexBufferAddresss = BindingData.pLocalVertexBuffer == nullptr ? 0 : BindingData.pLocalVertexBuffer->GetBufferDeviceAddress();
        uint64_t IndexBufferAddresss = BindingData.pLocalIndexBuffer == nullptr ? 0 : BindingData.pLocalIndexBuffer->GetBufferDeviceAddress();
        uint64_t CustomInfoBufferAddresss = BindingData.pLocalCustomInfoCbuffer == nullptr ? 0 : BindingData.pLocalCustomInfoCbuffer->GetBufferDeviceAddress();
        uint64_t LocalEngineBindlessBufferAddresss = BindingData.pLocalEngineBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialParamBufferAddresss = BindingData.pLocalMaterialParamCbuffer == nullptr ? 0 : BindingData.pLocalMaterialParamCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialBindlessBufferAddresss = BindingData.pLocalMaterialBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t data[] = { VertexBufferAddresss, IndexBufferAddresss, CustomInfoBufferAddresss, LocalEngineBindlessBufferAddresss, LocalMaterialParamBufferAddresss, LocalMaterialBindlessBufferAddresss };
        SetHitGroupShaderBindingData(BindingData.uRecordIndex, data, sizeof(data));

        return true;
    }

    bool RayTracingShaderBindingTableDx12::SetMissShaderBinding(const KRayTracingShaderBinding& BindingData, KRayTracingProgram* pPipeline)
    {
        RayTracingShaderIdentifier Identifier = ((RayTracingPipelineStateDx12*)pPipeline)->m_MissShaders.vecRayTracingShaderIdentifiers[BindingData.uShaderIndexInPipeline];
        SetMissShaderIdentifier(BindingData.uRecordIndex, Identifier);
        uint64_t VertexBufferAddresss = BindingData.pLocalVertexBuffer == nullptr ? 0 : BindingData.pLocalVertexBuffer->GetBufferDeviceAddress();
        uint64_t IndexBufferAddresss = BindingData.pLocalIndexBuffer == nullptr ? 0 : BindingData.pLocalIndexBuffer->GetBufferDeviceAddress();
        uint64_t CustomInfoBufferAddresss = BindingData.pLocalCustomInfoCbuffer == nullptr ? 0 : BindingData.pLocalCustomInfoCbuffer->GetBufferDeviceAddress();
        uint64_t LocalEngineBindlessBufferAddresss = BindingData.pLocalEngineBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialParamBufferAddresss = BindingData.pLocalMaterialParamCbuffer == nullptr ? 0 : BindingData.pLocalMaterialParamCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialBindlessBufferAddresss = BindingData.pLocalMaterialBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t data[] = { VertexBufferAddresss, IndexBufferAddresss, CustomInfoBufferAddresss, LocalEngineBindlessBufferAddresss, LocalMaterialParamBufferAddresss, LocalMaterialBindlessBufferAddresss };
        SetMissShaderBindingData(BindingData.uRecordIndex, data, sizeof(data));

        return true;
    }

    bool RayTracingShaderBindingTableDx12::SetRayGenShaderBinding(const KRayTracingShaderBinding& BindingData, KRayTracingProgram* pPipeline)
    {
        RayTracingShaderIdentifier Identifier = ((RayTracingPipelineStateDx12*)pPipeline)->m_RayGenShaders.vecRayTracingShaderIdentifiers[BindingData.uShaderIndexInPipeline];
        SetRayGenShaderIdentifier(BindingData.uRecordIndex, Identifier);
        uint64_t VertexBufferAddresss = BindingData.pLocalVertexBuffer == nullptr ? 0 : BindingData.pLocalVertexBuffer->GetBufferDeviceAddress();
        uint64_t IndexBufferAddresss = BindingData.pLocalIndexBuffer == nullptr ? 0 : BindingData.pLocalIndexBuffer->GetBufferDeviceAddress();
        uint64_t CustomInfoBufferAddresss = BindingData.pLocalCustomInfoCbuffer == nullptr ? 0 : BindingData.pLocalCustomInfoCbuffer->GetBufferDeviceAddress();
        uint64_t LocalEngineBindlessBufferAddresss = BindingData.pLocalEngineBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialParamBufferAddresss = BindingData.pLocalMaterialParamCbuffer == nullptr ? 0 : BindingData.pLocalMaterialParamCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialBindlessBufferAddresss = BindingData.pLocalMaterialBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t data[] = { VertexBufferAddresss, IndexBufferAddresss, CustomInfoBufferAddresss, LocalEngineBindlessBufferAddresss, LocalMaterialParamBufferAddresss, LocalMaterialBindlessBufferAddresss };
        SetRayGenShaderBindingData(BindingData.uRecordIndex, data, sizeof(data));

        return true;
    }

    bool RayTracingShaderBindingTableDx12::SetCallableShaderBinding(const KRayTracingShaderBinding& BindingData, KRayTracingProgram* pPipeline)
    {
        RayTracingShaderIdentifier Identifier = ((RayTracingPipelineStateDx12*)pPipeline)->m_CallableShaders.vecRayTracingShaderIdentifiers[BindingData.uShaderIndexInPipeline];
        SetCallableShaderIdentifier(BindingData.uRecordIndex, Identifier);
        uint64_t VertexBufferAddresss = BindingData.pLocalVertexBuffer == nullptr ? 0 : BindingData.pLocalVertexBuffer->GetBufferDeviceAddress();
        uint64_t IndexBufferAddresss = BindingData.pLocalIndexBuffer == nullptr ? 0 : BindingData.pLocalIndexBuffer->GetBufferDeviceAddress();
        uint64_t CustomInfoBufferAddresss = BindingData.pLocalCustomInfoCbuffer == nullptr ? 0 : BindingData.pLocalCustomInfoCbuffer->GetBufferDeviceAddress();
        uint64_t LocalEngineBindlessBufferAddresss = BindingData.pLocalEngineBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalEngineBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialParamBufferAddresss = BindingData.pLocalMaterialParamCbuffer == nullptr ? 0 : BindingData.pLocalMaterialParamCbuffer->GetBufferDeviceAddress();
        uint64_t LocalMaterialBindlessBufferAddresss = BindingData.pLocalMaterialBindlessIDCbuffer == nullptr ? 0 : BindingData.pLocalMaterialBindlessIDCbuffer->GetBufferDeviceAddress();
        uint64_t data[] = { VertexBufferAddresss, IndexBufferAddresss, CustomInfoBufferAddresss, LocalEngineBindlessBufferAddresss, LocalMaterialParamBufferAddresss, LocalMaterialBindlessBufferAddresss };
        SetCallableShaderBindingData(BindingData.uRecordIndex, data, sizeof(data));

        return true;
    }

    void RayTracingShaderBindingTableDx12::WriteData(uint32_t Offset, const void* pData, uint32_t Size)
    {
        ASSERT(pData != nullptr);

        if(Size != 0)
            memcpy(m_vecShaderBindingTableData.data() + Offset, pData, Size);
    }

    void RayTracingShaderBindingTableDx12::SetShaderIndentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier)
    {
        uint32_t Offset = RecordIndex * m_LocalRecordStride;
        WriteData(Offset, ShaderIdentifier.Data, ShaderIdentifierSize);
    }

    void RayTracingShaderBindingTableDx12::SetRayGenShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier)
    {
        uint32_t Offset = RecordIndex * m_LocalRecordStride;
        WriteData(Offset, ShaderIdentifier.Data, ShaderIdentifierSize);
    }

    void RayTracingShaderBindingTableDx12::SetHitGroupShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier)
    {
        uint32_t Offset = m_HitGroupShaderTableOffset + RecordIndex * m_LocalRecordStride;
        WriteData(Offset, ShaderIdentifier.Data, ShaderIdentifierSize);
    }

    void RayTracingShaderBindingTableDx12::SetMissShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIdentifier)
    {
        uint32_t Offset = m_MissShaderTableOffset + RecordIndex * m_LocalRecordStride;
        WriteData(Offset, ShaderIdentifier.Data, ShaderIdentifierSize);
    }

    void RayTracingShaderBindingTableDx12::SetCallableShaderIdentifier(uint32_t RecordIndex, const RayTracingShaderIdentifier& ShaderIndentifier)
    {
        uint32_t Offset = m_CallableShaderTableOffset + RecordIndex * m_LocalRecordStride;
        WriteData(Offset, ShaderIndentifier.Data, ShaderIdentifierSize);
    }

    void RayTracingShaderBindingTableDx12::SetBindingData(uint32_t Offset, uint32_t OffsetInRootSignature, const void* pData, uint32_t Size)
    {
        uint32_t ShaderTableOffset = Offset + OffsetInRootSignature;
        WriteData(ShaderTableOffset, pData, Size);
    }

    void RayTracingShaderBindingTableDx12::SetRayGenShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size)
    {
        uint32_t ShaderTableOffset = RecordIndex * m_LocalRecordStride;
        SetBindingData(ShaderTableOffset, ShaderIdentifierSize, pData, Size);
    }

    void RayTracingShaderBindingTableDx12::SetHitGroupShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size)
    {
        uint32_t ShaderTableOffset = m_HitGroupShaderTableOffset + RecordIndex * m_LocalRecordStride;
        SetBindingData(ShaderTableOffset, ShaderIdentifierSize, pData, Size);
    }

    void RayTracingShaderBindingTableDx12::SetMissShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size)
    {
        uint32_t ShaderTableOffset = m_MissShaderTableOffset + RecordIndex * m_LocalRecordStride;
        SetBindingData(ShaderTableOffset, ShaderIdentifierSize, pData, Size);
    }

    void RayTracingShaderBindingTableDx12::SetCallableShaderBindingData(uint32_t RecordIndex, const void* pData, uint32_t Size)
    {
        uint32_t ShaderTableOffset = m_CallableShaderTableOffset + RecordIndex * m_LocalRecordStride;
        SetBindingData(ShaderTableOffset, ShaderIdentifierSize, pData, Size);
    }

    static D3D12_RAYTRACING_GEOMETRY_TYPE TranslateRayTracingGeometryType(enumRayTracingGeometryType GeometryType)
    {
        switch (GeometryType)
        {
        case KRT_GT_TRIANGLE:
            return D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            break;
        case KRT_GT_PROCEDURAL:
            return D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
            break;
        default:
            return D3D12_RAYTRACING_GEOMETRY_TYPE(0);
        }
    }

    static DXGI_FORMAT TranslateRayTracingVertexFormat(enumVertexFormat VertexFormat)
    {
        switch (VertexFormat)
        {
        case VERT_FORMAT_R32G32B32A32_SFLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case VERT_FORMAT_R32G32B32_SFLOAT:
            return DXGI_FORMAT_R32G32B32_FLOAT;
            break;
        case VERT_FORMAT_R32G32_SFLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
            break;
        case VERT_FORMAT_R32_SFLOAT:
            return DXGI_FORMAT_R32_FLOAT;
            break;
        case VERT_FORMAT_R8G8B8A8_UINT:
            return DXGI_FORMAT_R8G8B8A8_UINT;
            break;
        case VERT_FORMAT_R8G8B8A8_SINT:
            return DXGI_FORMAT_R8G8B8A8_SINT;
            break;
        case VERT_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case VERT_FORMAT_R8G8B8A8_SNORM:
            return DXGI_FORMAT_R8G8B8A8_SNORM;
            break;
        case VERT_FORMAT_R16G16_UINT:
            return DXGI_FORMAT_R16G16_UINT;
            break;
        case VERT_FORMAT_R16G16_SINT:
            return DXGI_FORMAT_R16G16_SINT;
            break;
        default:
            return DXGI_FORMAT_UNKNOWN;
            break;
        }
    }

    static DXGI_FORMAT TranslateRayTracingIndexFormat(enumIndexType IndexType)
    {
        switch (IndexType)
        {
        case INDEX_TYPE_UINT16:
            return DXGI_FORMAT_R16_UINT;
            break;
        case INDEX_TYPE_UINT32:
            return DXGI_FORMAT_R32_UINT;
            break;
        default:
            return DXGI_FORMAT_UNKNOWN;
            break;
        }
    }

    static RayTracingAccelerationStructureSize BuildRayTracingGeometryDesc(const RayTracingGeomeryCreateDesc& InGeometryCreateDesc, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& OutvecDescs, IKGFX_RenderContext* pRenderContext, bool bPrebuild)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12_RAYTRACING_GEOMETRY_TYPE GeometryType = TranslateRayTracingGeometryType(InGeometryCreateDesc.eGeometryType);
        RayTracingAccelerationStructureSize SizeInfo{};
        KGfxBarrier Barrier{};
        std::vector< D3D12_RAYTRACING_GEOMETRY_DESC> vecDescs;

        if (!bPrebuild)
        {
            Barrier.eDSTAccess = KGfxAccess::BVHRead;
            Barrier.pBuffer = InGeometryCreateDesc.pIndexBuffer;
            Barrier.eType = KGfxBarrier::EType::Buffer;
            pRenderContext->Transition(Barrier);
        }

        for (uint32_t SegmentIndex = 0; SegmentIndex < InGeometryCreateDesc.uSegmentsCount; ++SegmentIndex)
        {
            const RayTracingGeomerySegment& GeometrySegment = InGeometryCreateDesc.pSegments[SegmentIndex];
            D3D12_RAYTRACING_GEOMETRY_DESC GeometryDesc = {};
            KGfxBarrier Barrier{};

            if (!bPrebuild)
            {
                Barrier.eDSTAccess = KGfxAccess::BVHRead;
                Barrier.pBuffer = GeometrySegment.pVertexBuffer;
                Barrier.eType = KGfxBarrier::EType::Buffer;
                pRenderContext->Transition(Barrier);
            }

            GeometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            GeometryDesc.Type = GeometryType;

            if (GeometrySegment.bOpaque)
            {
                GeometryDesc.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            }

            GeometryDesc.Triangles.VertexFormat = TranslateRayTracingVertexFormat(GeometrySegment.eVertFormat);

            switch (InGeometryCreateDesc.eGeometryType)
            {
            case KRT_GT_TRIANGLE:
                if (InGeometryCreateDesc.pIndexBuffer)
                {
                    GeometryDesc.Triangles.IndexCount = GeometrySegment.uNumPrimitive * KGFX_RayTracingGeometryDx12::uIndicesPerPrimitive;
                    GeometryDesc.Triangles.IndexFormat = TranslateRayTracingIndexFormat(InGeometryCreateDesc.eIndexType);
                    GeometryDesc.Triangles.IndexBuffer = InGeometryCreateDesc.pIndexBuffer->GetBufferDeviceAddress() + InGeometryCreateDesc.uIndexBufferOffset + GeometrySegment.uFirstPrimitive * (GeometryDesc.Triangles.IndexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4);
                    GeometryDesc.Triangles.VertexCount = GeometrySegment.uVertexCount;
                    GeometryDesc.Triangles.VertexBuffer.StartAddress = GeometrySegment.pVertexBuffer->GetBufferDeviceAddress() + GeometrySegment.uVertexBufferOffset;
                    GeometryDesc.Triangles.VertexBuffer.StrideInBytes = GeometrySegment.uVertexBufferStride;
                }
                else
                {
                    GeometryDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
                    GeometryDesc.Triangles.VertexCount = GeometrySegment.uVertexCount;
                }
                break;

            case KRT_GT_PROCEDURAL:
                GeometryDesc.AABBs.AABBCount = GeometrySegment.uNumPrimitive;
                GeometryDesc.AABBs.AABBs.StartAddress = GeometrySegment.pVertexBuffer->GetBufferDeviceAddress() + GeometrySegment.uVertexBufferOffset;
                GeometryDesc.AABBs.AABBs.StrideInBytes = GeometrySegment.uVertexBufferStride;
                break;

            default:
                break;
            }

            vecDescs.push_back(GeometryDesc);
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS PrebuildDescInputs{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO PrebuildInfo{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlag = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        BuildFlag = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;// InGeometryCreateDesc.bAllowUpdate ? BuildFlag | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE : BuildFlag;

        PrebuildDescInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        PrebuildDescInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        PrebuildDescInputs.NumDescs = (UINT)vecDescs.size();
        PrebuildDescInputs.pGeometryDescs = vecDescs.data();
        PrebuildDescInputs.Flags = BuildFlag;

        pGraphicDevice->GetDXDevice5()->GetRaytracingAccelerationStructurePrebuildInfo(&PrebuildDescInputs, &PrebuildInfo);
        SizeInfo.ResultSize = std::max(SizeInfo.ResultSize, PrebuildInfo.ResultDataMaxSizeInBytes);
        SizeInfo.BuildScratchSize = std::max(SizeInfo.BuildScratchSize, PrebuildInfo.ScratchDataSizeInBytes);
        SizeInfo.UpdateScratchSize = std::max(SizeInfo.UpdateScratchSize, PrebuildInfo.UpdateScratchDataSizeInBytes);

        SizeInfo.ResultSize = Align(SizeInfo.ResultSize, RayTracingAccelerationStructureAlignment);
        SizeInfo.BuildScratchSize = Align(SizeInfo.BuildScratchSize, RayTracingScratchBufferAlignment);
        SizeInfo.UpdateScratchSize = Align(SizeInfo.UpdateScratchSize, RayTracingScratchBufferAlignment);
        if (!bPrebuild)
        {
            OutvecDescs = vecDescs;
        }

        return SizeInfo;
    }

    static RayTracingAccelerationStructureSize CalAccelerationStructureSize(uint32_t Count, bool bUpdate)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO PrebuildInfo{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS PreBuildInput{};
        RayTracingAccelerationStructureSize SizeInfo{};

        PreBuildInput.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        PreBuildInput.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;// bUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        PreBuildInput.NumDescs = Count;
        PreBuildInput.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        pGraphicDevice->GetDXDevice5()->GetRaytracingAccelerationStructurePrebuildInfo(&PreBuildInput, &PrebuildInfo);

        SizeInfo.BuildScratchSize = Align(PrebuildInfo.ScratchDataSizeInBytes, RayTracingAccelerationStructureAlignment);
        SizeInfo.ResultSize = Align(PrebuildInfo.ResultDataMaxSizeInBytes, RayTracingScratchBufferAlignment);
        SizeInfo.UpdateScratchSize = Align(PrebuildInfo.UpdateScratchDataSizeInBytes, RayTracingScratchBufferAlignment);

        return SizeInfo;
    }

    KGFX_RayTracingGeometryDx12::~KGFX_RayTracingGeometryDx12()
    {
        SAFE_RELEASE(m_pBottomAccelerationStructureBuffer);
    }

    void KGFX_RayTracingGeometryDx12::Destroy()
    {
        SAFE_RELEASE(m_pBottomAccelerationStructureBuffer);
    }

    BOOL KGFX_RayTracingGeometryDx12::Create(const RayTracingGeomeryCreateDesc& InGeometryCreateDesc, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;

        IKGFX_GraphicDevice* pGraphicDevice = KGFX_GetGraphicDevice();
        m_GeometryCreateDesc = InGeometryCreateDesc;
        m_SizeInfo = BuildRayTracingGeometryDesc(InGeometryCreateDesc, m_vecGeometryDescs, pRenderContext, true);

        KGfxBufferDesc BufferDesc{};
        BufferDesc.uByteWidth = (uint32_t)m_SizeInfo.ResultSize;
        BufferDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        
        nRetCode = pGraphicDevice->CreateBuffer(&m_pBottomAccelerationStructureBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    BOOL KGFX_RayTracingGeometryDx12::Update(const RayTracingGeomeryUpdateParams& InUpdateParams, RayTracingAccelerationStructureSize& OutPreBuildSizeInfo, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        RayTracingAccelerationStructureSize TmpPreBuildSizeInfo{};
        KGLOG_ASSERT_EXIT(m_pBottomAccelerationStructureBuffer != nullptr);

        m_vecGeometryDescs.clear();
        m_GeometryCreateDesc.pSegments = InUpdateParams.pSegments;
        m_GeometryCreateDesc.uSegmentsCount = InUpdateParams.uSegmentsCount;
        TmpPreBuildSizeInfo = BuildRayTracingGeometryDesc(m_GeometryCreateDesc, m_vecGeometryDescs, pRenderContext, false);

        KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.ResultSize <= m_SizeInfo.ResultSize);
        if (InUpdateParams.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_UPDATE)
        {
            KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.UpdateScratchSize <= m_SizeInfo.UpdateScratchSize);
        }
        else
        {
            KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.BuildScratchSize <= m_SizeInfo.BuildScratchSize);
        }

        OutPreBuildSizeInfo = TmpPreBuildSizeInfo;
        nResult = TRUE;
    Exit0:
        return nResult;
    }

    RayTracingSceneDx12::~RayTracingSceneDx12()
    {
        Destroy();
    }

    void RayTracingSceneDx12::Destroy()
    {
        SAFE_RELEASE(m_pTopAccelerationStructureView);
        SAFE_RELEASE(m_pTopAccelerationStructureBuffer);
        SAFE_RELEASE(m_pInstanceBuffer);

        for (auto Iter : m_mapShaderTables)
        {
            delete Iter.second;
        }
        m_mapShaderTables.clear();
    }

    uint32_t RayTracingSceneDx12::GetBindlessHandle()
    {
        assert(m_pTopAccelerationStructureView != nullptr);

        return m_pTopAccelerationStructureView->GetBindlessHandle();
    }

    BOOL RayTracingSceneDx12::Create(const RayTracingSceneCreateDesc& SceneCreateDesc, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;

        IKGFX_GraphicDevice* pGraphicDevice = KGFX_GetGraphicDevice();
        m_SizeInfo = CalAccelerationStructureSize(SceneCreateDesc.uMaxGeometryInstanceCount, true);
        m_SceneCreateDesc = SceneCreateDesc;
        gfx::KGFX_BufferViewDesc BufferViewDesc{};

        KGfxBufferDesc BufferDesc{};
        BufferDesc.uByteWidth = (uint32_t)m_SizeInfo.ResultSize;
        BufferDesc.uUsageFlags = gfx::BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        BufferDesc.eMiscFlags = 0;

        nRetCode = pGraphicDevice->CreateBuffer(&m_pTopAccelerationStructureBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        BufferViewDesc.uBytesRange = (uint32_t)m_SizeInfo.ResultSize;
        BufferViewDesc.eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV;

        nRetCode = pGraphicDevice->CreateBufferView(m_pTopAccelerationStructureBuffer, BufferViewDesc, &m_pTopAccelerationStructureView, "TopAccelerationStructure");
        KGLOG_ASSERT_EXIT(nRetCode);

        BufferDesc.uByteWidth = SceneCreateDesc.uMaxGeometryInstanceCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        BufferDesc.uUsageFlags = BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;

        nRetCode = pGraphicDevice->CreateBuffer(&m_pInstanceBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    BOOL RayTracingSceneDx12::Update(const RayTracingSceneUpdateParams& SceneUpdateParam, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        KGfxBarrier Barrier{};
        std::vector< D3D12_RAYTRACING_INSTANCE_DESC> vecInstanceDescs;

        BOOL bUpdate = SceneUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? false : true;
        RayTracingAccelerationStructureSize TmpPreBuildSizeInfo = CalAccelerationStructureSize(SceneUpdateParam.uInstanceCount, bUpdate);
        KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.ResultSize <= m_SizeInfo.ResultSize);
        if (SceneUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_UPDATE)
        {
            KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.UpdateScratchSize <= m_SizeInfo.UpdateScratchSize);
        }
        else
        {
            KGLOG_ASSERT_EXIT(TmpPreBuildSizeInfo.BuildScratchSize <= m_SizeInfo.BuildScratchSize);
        }

        vecInstanceDescs.resize(SceneUpdateParam.uInstanceCount);
        for (uint32_t InstanceCount = 0; InstanceCount < SceneUpdateParam.uInstanceCount; ++InstanceCount)
        {
            RayTracingInstance& pInstance = SceneUpdateParam.pInstance[InstanceCount];
            vecInstanceDescs[InstanceCount].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            vecInstanceDescs[InstanceCount].InstanceID = pInstance.uInstanceID;
            vecInstanceDescs[InstanceCount].InstanceMask = 0xFF;
            vecInstanceDescs[InstanceCount].InstanceContributionToHitGroupIndex = pInstance.uHitGroupOffset;
            const NSKMath::KMatrix34 Trans34Mat = pInstance.pTransform;
            memcpy(vecInstanceDescs[InstanceCount].Transform, &Trans34Mat, sizeof(Trans34Mat));
            vecInstanceDescs[InstanceCount].AccelerationStructure = ((KGFX_RayTracingGeometryDx12*)pInstance.pGeometry)->GetBottomAccelerationStructureBuffer()->GetBufferDeviceAddress();
        }

        if (!vecInstanceDescs.empty())
        {
            Barrier.eDSTAccess = KGfxAccess::CopyDst;
            Barrier.pBuffer = m_pInstanceBuffer;
            Barrier.eType = KGfxBarrier::EType::Buffer;
            pRenderContext->Transition(Barrier);

            pRenderContext->CmdUpdateSubResource(m_pInstanceBuffer, 0, (uint32_t)vecInstanceDescs.size() * sizeof(vecInstanceDescs[0]), vecInstanceDescs.data());
        }

        Barrier.eDSTAccess = KGfxAccess::BVHRead;
        Barrier.pBuffer = m_pInstanceBuffer;
        Barrier.eType = KGfxBarrier::EType::Buffer;
        pRenderContext->Transition(Barrier);

        nResult = TRUE;
    Exit0:
        return nResult;
    }

    RayTracingShaderBindingTableDx12* RayTracingSceneDx12::FindOrCreateShaderBindingTable(const RayTracingPipelineStateDx12* pPipeline)
    {
        BOOL nRetCode = FALSE;
        RayTracingShaderBindingTableDx12* pResultShaderBindingTable = nullptr;
        ShaderBindingTableDesc Desc{};
        if (m_mapShaderTables.find(pPipeline) != m_mapShaderTables.end())
        {
            pResultShaderBindingTable = m_mapShaderTables.find(pPipeline)->second;
            return pResultShaderBindingTable;
        }

        pResultShaderBindingTable = new RayTracingShaderBindingTableDx12;
        Desc.uInitHitGroupCount = (uint32_t)pPipeline->m_HitGroupShaders.vecRayTracingShaders.size();
        Desc.uInitMissCount = (uint32_t)pPipeline->m_MissShaders.vecRayTracingShaders.size();
        Desc.uInitRayGenCount = 1;
        Desc.uInitCallableCount = (uint32_t)pPipeline->m_CallableShaders.vecRayTracingShaders.size();
        Desc.uRayGenRecordAlignedSizeInByte = 0;
        Desc.uMissRecordAlignedSizeInByte = 0;
        Desc.uHitRecordAlignedSizeInByte = sizeof(uint64_t) * 4;
        Desc.uCallableRecordAlignedSizeInByte = 0;
        nRetCode = pResultShaderBindingTable->Create(Desc);
        KGLOG_ASSERT_EXIT(nRetCode);

    Exit0:
        return pResultShaderBindingTable;
    }

    static BOOL SetRayTracingLocalShaderResources(
        IKGFX_RenderContext* pRenderContext,
        const RayTracingShaderDx12* pHitGroupShader,
        RayTracingShaderBindingTableDx12* pShaderBindingTable,
        IKGFX_BufferView* pBufferViews, uint32_t NumBufferViews,
        uint32_t LooseParameterDataSize, const void* pLooseParameterData)
    {
        BOOL nRetCode = FALSE;
        BOOL nResult = FALSE;



        nResult = TRUE;

        return nResult;
    }

    KRayTracingProgram* RayTracingDx12Proxy::CommitRayTracingProgram(const RayTracingProgramDesc& rtpDC)
    {
        RayTracingPipelineStateDx12* pResultProgram = new RayTracingPipelineStateDx12;
        bool bRetCode = pResultProgram->Create(rtpDC);
        assert(bRetCode);

        return pResultProgram;
    }

    KRayTracingGeomery* RayTracingDx12Proxy::CreateRHIRayTracingGeomtry()
    {
        KGFX_RayTracingGeometryDx12* pGeometry = new KGFX_RayTracingGeometryDx12;
        return pGeometry;
    }

    bool RayTracingDx12Proxy::InitRHIRayTracingGeometry(const RayTracingGeomeryCreateDesc& GeometryCreateDesc, KRayTracingGeomery* pRHIGeometry)
    {
        bool bRetCode = FALSE;
        bool bResult = FALSE;

        bRetCode = ((KGFX_RayTracingGeometryDx12*)pRHIGeometry)->Create(GeometryCreateDesc);
        KG_ASSERT_EXIT(bRetCode);

        bResult = TRUE;
    Exit0:
        return bResult;
    }

    BOOL RayTracingDx12Proxy::CommitRHIRayTracingGeometries(const RayTracingGeometryUpdateBatch& GeometryUpdateBatch, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nResult = FALSE;
        BOOL nRetCode = FALSE;
        IKGFX_Buffer* pScratchBuffer = nullptr;
        IKGFX_GraphicDevice* pGraphicDevice = KGFX_GetGraphicDevice();
        KGFX_CommandBufferDX12Impl* Dx12CommandList = (KGFX_CommandBufferDX12Impl*)pRenderContext;
        ID3D12GraphicsCommandList4* pCommandList4 = Dx12CommandList->GetD3D12CommandList4();
        KGfxBufferDesc BufferDesc{};
        uint64_t MaxScratchSize = 0;
        std::vector<D3D12_RESOURCE_BARRIER> vecResultBarriers;
        gfx::KGfxBarrier ScratchBarrier;
        KGLOG_ASSERT_EXIT(pCommandList4);
        KG_PROCESS_SUCCESS(GeometryUpdateBatch.uGeometryCount == 0);

        for (uint32_t GeometryCount = 0; GeometryCount < GeometryUpdateBatch.uGeometryCount; ++GeometryCount)
        {
            RayTracingAccelerationStructureSize SizeInfo{};
            RayTracingGeomeryUpdateParams& pUpdateParam = GeometryUpdateBatch.pPerGeometryUpdateParams[GeometryCount];
            KRayTracingGeomery* pGeometry = GeometryUpdateBatch.ppGeometries[GeometryCount];

            nRetCode = ((KGFX_RayTracingGeometryDx12*)pGeometry)->Update(pUpdateParam, SizeInfo, pRenderContext);
            KGLOG_ASSERT_EXIT(nRetCode);

            bool bUpdate = pUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? false : true;
            MaxScratchSize = std::max(MaxScratchSize, std::max(SizeInfo.BuildScratchSize, SizeInfo.UpdateScratchSize));
        }

        BufferDesc.uByteWidth = (uint32_t)MaxScratchSize;
        BufferDesc.uUsageFlags = gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;

        nRetCode = pGraphicDevice->CreateBuffer(&pScratchBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        //gfx::KGfxBarrier ScratchBarrier;
        ScratchBarrier.pBuffer = pScratchBuffer;
        ScratchBarrier.eType = gfx::KGfxBarrier::EType::Buffer;
        ScratchBarrier.eDSTAccess = KGfxAccess::UAVMask;
        ScratchBarrier.eSRCAccess = KGfxAccess::Unknown;
        pRenderContext->Transition(ScratchBarrier);

        for(uint32_t GeometryCount = 0; GeometryCount < GeometryUpdateBatch.uGeometryCount; ++GeometryCount)
        {
            RayTracingGeomeryUpdateParams& pUpdateParam = GeometryUpdateBatch.pPerGeometryUpdateParams[GeometryCount];
            KRayTracingGeomery* pGeometry = GeometryUpdateBatch.ppGeometries[GeometryCount];
            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& vecGeometryDescs = ((KGFX_RayTracingGeometryDx12*)pGeometry)->GetRayTracingGeometryDesc();

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Input{};
            Input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;// pUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            Input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            Input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            Input.NumDescs = (UINT)vecGeometryDescs.size();
            Input.pGeometryDescs = vecGeometryDescs.data();

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC BuildDesc{};
            BuildDesc.Inputs = Input;
            BuildDesc.ScratchAccelerationStructureData = pScratchBuffer->GetBufferDeviceAddress();
            BuildDesc.SourceAccelerationStructureData = 0;// pUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? 0 : ((KGFX_RayTracingGeometryDx12*)pGeometry)->GetBottomAccelerationStructureBuffer()->GetBufferDeviceAddress();
            BuildDesc.DestAccelerationStructureData = ((KGFX_RayTracingGeometryDx12*)pGeometry)->GetBottomAccelerationStructureBuffer()->GetBufferDeviceAddress();

            pCommandList4->BuildRaytracingAccelerationStructure(&BuildDesc, 0, nullptr);

            //gfx::KGfxBarrier ScratchBarrier;
            ScratchBarrier.pBuffer = pScratchBuffer;
            ScratchBarrier.eType = gfx::KGfxBarrier::EType::Buffer;
            ScratchBarrier.eDSTAccess = KGfxAccess::UAVMask;
            ScratchBarrier.eSRCAccess = KGfxAccess::Unknown;
            pRenderContext->Transition(ScratchBarrier);
        }

        vecResultBarriers.resize(GeometryUpdateBatch.uGeometryCount);
        for (uint32_t GeometryCount = 0; GeometryCount < GeometryUpdateBatch.uGeometryCount; ++GeometryCount)
        {
            KRayTracingGeomery* pGeometry = GeometryUpdateBatch.ppGeometries[GeometryCount];
            vecResultBarriers[GeometryCount].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            vecResultBarriers[GeometryCount].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            vecResultBarriers[GeometryCount].UAV.pResource = ((KGFX_BufferDx12*)((KGFX_RayTracingGeometryDx12*)pGeometry)->GetBottomAccelerationStructureBuffer())->GetBufferImpl()->GetBufResource();
        }
        pCommandList4->ResourceBarrier((uint32_t)vecResultBarriers.size(), vecResultBarriers.data());

    Exit1:
        nResult = TRUE;
    Exit0:
        SAFE_RELEASE(pScratchBuffer);
        return nResult;
    }

    KRayTracingScene* RayTracingDx12Proxy::CreateRHIRayTracingScene(const RayTracingSceneCreateDesc& SceneCreateDesc)
    {
        BOOL nRetCode = FALSE;

        RayTracingSceneDx12* pRayTracingScene = new RayTracingSceneDx12();
        nRetCode = pRayTracingScene->Create(SceneCreateDesc);
        if (!nRetCode)
        {
            pRayTracingScene->Destroy();
            SAFE_DELETE(pRayTracingScene);
        }

        return pRayTracingScene;
    }

    BOOL RayTracingDx12Proxy::CommitRHIRayTracingScene(const RayTracingSceneUpdateParams& SceneUpdateParam, IKGFX_RenderContext* pRenderContext)
    {
        BOOL nRetCode = FALSE;
        BOOL nResult = FALSE;
        IKGFX_Buffer* pScratchBuffer = nullptr;
        IKGFX_GraphicDevice* pGraphicDevice = KGFX_GetGraphicDevice();
        KGFX_CommandBufferDX12Impl* Dx12CommandList = (KGFX_CommandBufferDX12Impl*)pRenderContext;
        ID3D12GraphicsCommandList4* pCommandList4 = Dx12CommandList->GetD3D12CommandList4();
        bool bUpdate = false;
        KGfxBufferDesc BufferDesc{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Input{};
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC BuildDesc{};
        RayTracingAccelerationStructureSize SizeInfo{};
        D3D12_RESOURCE_BARRIER ResultBarrier{};
        gfx::KGfxBarrier ScratchBarrier;
        KGLOG_ASSERT_EXIT(pCommandList4);
        KGLOG_ASSERT_EXIT(SceneUpdateParam.pScene != nullptr);
        KG_PROCESS_SUCCESS(SceneUpdateParam.uInstanceCount == 0);

        nRetCode = ((RayTracingSceneDx12*)SceneUpdateParam.pScene)->Update(SceneUpdateParam, pRenderContext);
        KGLOG_ASSERT_EXIT(nRetCode);

        bUpdate = SceneUpdateParam.eBuildMode == enumAccelerationStructureBuildMode::KRT_BM_BUILD ? false : true;
        SizeInfo = CalAccelerationStructureSize(SceneUpdateParam.uInstanceCount, bUpdate);
        BufferDesc.uByteWidth = (uint32_t)SizeInfo.BuildScratchSize;// bUpdate ? (uint32_t)SizeInfo.UpdateScratchSize : (uint32_t)SizeInfo.BuildScratchSize;
        BufferDesc.uUsageFlags = gfx::BUFFER_USAGE_STORAGE_BUFFER_BIT;
        BufferDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;

        nRetCode = pGraphicDevice->CreateBuffer(&pScratchBuffer, BufferDesc, nullptr);
        KGLOG_ASSERT_EXIT(nRetCode);

        ScratchBarrier.pBuffer = pScratchBuffer;
        ScratchBarrier.eType = gfx::KGfxBarrier::EType::Buffer;
        ScratchBarrier.eDSTAccess = KGfxAccess::UAVMask;
        ScratchBarrier.eSRCAccess = KGfxAccess::Unknown;
        pRenderContext->Transition(ScratchBarrier);

        Input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        Input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;// bUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        Input.NumDescs = SceneUpdateParam.uInstanceCount;
        Input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        BuildDesc.Inputs = Input;
        BuildDesc.Inputs.InstanceDescs = ((RayTracingSceneDx12*)SceneUpdateParam.pScene)->GetInstanceBuffer()->GetBufferDeviceAddress();
        BuildDesc.ScratchAccelerationStructureData = pScratchBuffer->GetBufferDeviceAddress();
        BuildDesc.SourceAccelerationStructureData = 0;// bUpdate ? ((RayTracingSceneDx12*)SceneUpdateParam.pScene)->GetTopAccelerationStructureBuffer()->GetBufferDeviceAddress() : 0;
        BuildDesc.DestAccelerationStructureData = ((RayTracingSceneDx12*)SceneUpdateParam.pScene)->GetTopAccelerationStructureBuffer()->GetBufferDeviceAddress();

        pCommandList4->BuildRaytracingAccelerationStructure(&BuildDesc, 0, nullptr);
        ResultBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        ResultBarrier.UAV.pResource = ((KGFX_BufferDx12*)((RayTracingSceneDx12*)SceneUpdateParam.pScene)->GetTopAccelerationStructureBuffer())->GetBufferImpl()->GetBufResource();
        pCommandList4->ResourceBarrier(1, &ResultBarrier);

    Exit1:
        nResult = TRUE;
    Exit0:
        SAFE_RELEASE(pScratchBuffer);
        return nResult;
    }

    KShaderBindingTable* RayTracingDx12Proxy::CreateRHIShaderBindingTable(const ShaderBindingTableDesc& SBTDC, KRayTracingProgram* pProgram)
    {
        BOOL nRetCode = FALSE;

        RayTracingShaderBindingTableDx12* pBindingTable = new RayTracingShaderBindingTableDx12;
        nRetCode = pBindingTable->Create(SBTDC);
        assert(nRetCode);

        return pBindingTable;
    }

    bool RayTracingDx12Proxy::TraceRay(IRayTracingShader* pRayGenShader, IRayTracingShader* pMissShader, IRayTracingShader* pCallableShader, KRayTracingProgram* pRayTracingProgram, KShaderBindingTable* pShaderBindingTable, uint32_t Width, uint32_t Height, IKGFX_RenderContext* pRenderContext)
    {
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        D3D12BindlessDescriptorHeapManager* pBindlessHeapManager = pGraphicDevice->GetDX12BindlessHeapManager();
        ID3D12RootSignature* pRootSignature = pGraphicDevice->GetRayTracingGlobalRootSignature()->GetDeviceRootSignature();
        KGFX_CommandBufferDX12Impl* Dx12CommandList = (KGFX_CommandBufferDX12Impl*)pRenderContext;
        ID3D12GraphicsCommandList4* pDeviceCommandList = Dx12CommandList->GetD3D12CommandList4();
        RayTracingShaderDx12* pRayGenShaderDx12 = (RayTracingShaderDx12*)pRayGenShader;

        ID3D12DescriptorHeap* pResourHeap = pBindlessHeapManager->GetHeap(BindlessHeapType::Standard, BindlessConfiguration::RayTracingShader)->GetGPUHeap();
        ID3D12DescriptorHeap* pSamplerHeap = pBindlessHeapManager->GetHeap(BindlessHeapType::Sampler, BindlessConfiguration::RayTracingShader)->GetGPUHeap();
        ID3D12DescriptorHeap* pHeaps[2] = { pResourHeap, pSamplerHeap };

        pDeviceCommandList->SetDescriptorHeaps(2, pHeaps);

        ((RayTracingPipelineStateDx12*)pRayTracingProgram)->Apply(pRenderContext, pRayGenShaderDx12, (RayTracingShaderBindingTableDx12*)pShaderBindingTable);

        D3D12_DISPATCH_RAYS_DESC DispatchRayDesc = ((RayTracingShaderBindingTableDx12*)pShaderBindingTable)->GetDispatchDesc();
        DispatchRayDesc.Width = Width;
        DispatchRayDesc.Height = Height;
        DispatchRayDesc.Depth = 1;

        pDeviceCommandList->SetComputeRootSignature(pRootSignature);
        pDeviceCommandList->SetPipelineState1(((RayTracingPipelineStateDx12*)pRayTracingProgram)->GetD3D12StateObject());
        pDeviceCommandList->DispatchRays(&DispatchRayDesc);
        Dx12CommandList->InvalidateDescriptorHeapBinding();
        return true;
    }

    IRayTracingShader* RayTracingDx12Proxy::CreateRayTracingShader(const KRayTracingShaderCreateDesc& ShaderDesc)
    {
        bool bRetCode = FALSE;

        RayTracingShaderDx12* pShader = new RayTracingShaderDx12;
        bRetCode = pShader->Create(ShaderDesc);
        assert(bRetCode);

        return pShader;
    }
}
