#ifdef _WIN32
#include "KGFX_ShaderResourceDx12.h"
#include "KGFX_DXCComplieDx12.h"
#include <D3Dcompiler.h>
#include "KGFX_GraphiceDeviceDx12.h"
#include "KBase/Public/io/KFile.h"
#include "../comm/KGFX_ShaderHelper.h"
#include <sstream>
#include "KBase/Public/str/KUtf8Convert.h"
#include <regex>
#include <rapidjson/document.h>
////////////////////////////////////////////////////////////
#include "KGFX_HashFunDX12.h"
#include "KBase/Public/KMemLeak.h"
#include "KEnginePub/Private/comm/KGFX_StaticConstBuffer.h"


namespace gfx
{
    static KGFX_ShaderResourcePoolDx12* g_pShaderResourePoolDx12 = nullptr;

    KGFX_ShaderTechItemDX12::~KGFX_ShaderTechItemDX12()
    {
        SAFE_RELEASE(m_pMainShaderFile);
        m_pMainShaderFile = nullptr;
        m_MainShaderPath.clear();
        m_UserShaderPath.clear();
        m_EntryPoint.clear();
        m_Key.clear();
        m_TechMacroDXC.clear();
    }

    bool KGFX_ShaderTechItemDX12::LoadShader()
    {
        BOOL bResult = false;

        gfx::KGFX_ShaderFilePool* pShaderFilePool = gfx::KGFX_GetShaderFilePool();
        KUniqueStr                ustrShaderPath = g_CachePathString(m_MainShaderPath.string().c_str(), TRUE);

        NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
        SAFE_RELEASE(m_pMainShaderFile);
        m_pMainShaderFile = pShaderFilePool->OnlyLoadShaderFile(sShaderLoc, nullptr);
        KGLOG_PROCESS_ERROR(m_pMainShaderFile);

        bResult = true;
    Exit0:
        return bResult;
    }

    KGFX_ShaderDx12::KGFX_ShaderDx12()
    {
        m_pCompiledShader = nullptr;
    }

    KGFX_ShaderDx12::~KGFX_ShaderDx12()
    {
        SAFE_RELEASE(m_pCompiledShader);
    }

    int32_t KGFX_ShaderDx12::AddRef()
    {
        return ++m_nRef;
    }

    int32_t KGFX_ShaderDx12::GetRef()
    {
        return m_nRef;
    }

    int32_t KGFX_ShaderDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            KGFX_ShaderResourcePoolDx12* pPool = KGFX_GetShaderPoolDx12();
            BOOL                         bRet = pPool->RemoveShader(m_szKey);
            ASSERT(bRet);
        }
        return nRef;
    }

    const char* KGFX_ShaderDx12::GetKey() const
    {
        return m_szKey.c_str();
    }

    void KGFX_ShaderDx12::SetKey(const char* pKey)
    {
        m_szKey = pKey;
    }

    ShaderStageType KGFX_ShaderDx12::GetShaderStage() const
    {
        return m_pShaderTechItem.m_ShageStage;
    }

    ID3DBlob* KGFX_ShaderDx12::GetCompliledShader() const
    {
        return m_pCompiledShader;
    }

    uint64_t KGFX_ShaderDx12::GetHashCode() const
    {
        return  m_uHashCode;
    }

    KGFX_ShaderReflectorDx12* KGFX_ShaderDx12::GetShaderRefl()
    {
        return m_pReflector;
    }

    BOOL KGFX_ShaderDx12::LoadShaderDXC(ShaderStageType eShaderStage, const char* key, const char* ShaderPath, const char* szEntryName, const KUniqueStr& sUserShaderPath, const char* szUserDefMacro, const char* szFileDefMacro)
    {
        BOOL bResult = false;
        BOOL bRetCode = false;

        m_pShaderTechItem.m_EntryPoint = szEntryName;
        m_pShaderTechItem.m_MainShaderPath = ShaderPath;
        m_pShaderTechItem.m_ShageStage = eShaderStage;
        m_pShaderTechItem.m_Key = key;

        if (sUserShaderPath.IsValid())
        {
            m_pShaderTechItem.m_UserShaderPath = sUserShaderPath.Str();
        }

        if (szUserDefMacro && szUserDefMacro[0])
        {
            IncludeFileHelper::ExpandMacoDefineDXC(szUserDefMacro, m_pShaderTechItem.m_TechMacroDXC);
        }

        if (szFileDefMacro && szFileDefMacro[0])
        {
            IncludeFileHelper::ExpandMacoDefineDXC(szFileDefMacro, m_pShaderTechItem.m_TechMacroDXC);
        }

        m_pShaderTechItem.LoadShader();

        bRetCode = ComplieDXC();
        KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        return bResult;
    }

    BOOL KGFX_ShaderDx12::ComplieDXC()
    {
        auto                     dxcComp = KGFX_GetDXCComplier();
        ID3D12ShaderReflection* pReflection = nullptr;
        std::string              errorMsg = {};
        std::vector<std::string> errors, warnings, notes, debugInfo;
        std::regex               errorRegex(R"((.*error:.*))");
        std::regex               warningRegex(R"((.*warning:.*))");
        std::regex               noteRegex(R"((.*note:.*))");
        std::smatch              match;
        std::istringstream       iss;
        std::string              line;

        IDxcBlob* pBlob = nullptr;
        bool      bres = false;
        bres = dxcComp->CheckShaderCache(&m_pShaderTechItem, &pReflection, &pBlob, errorMsg);
        KGLOG_PROCESS_ERROR(bres);

        iss.str(errorMsg);
        while (std::getline(iss, line))
        {
            if (std::regex_search(line, match, errorRegex))
            {
                errors.push_back(line);
            }
            else if (std::regex_search(line, match, warningRegex))
            {
                warnings.push_back(line);
            }
            else if (std::regex_search(line, match, noteRegex))
            {
                notes.push_back(line);
            }
            else
            {
                debugInfo.push_back(line);
            }
        }

        for (auto& msg : errors)
        {
            KGLogPrintf(KGLOG_ERR, msg.data());
        }

        for (auto& msg : warnings)
        {
            KGLogPrintf(KGLOG_WARNING, msg.data());
        }

        for (auto& msg : notes)
        {
            KGLogPrintf(KGLOG_INFO, msg.data());
        }

        for (auto& msg : debugInfo)
        {
            KGLogPrintf(KGLOG_DEBUG, msg.data());
        }

        KGLOG_PROCESS_ERROR(pReflection);

        m_pCompiledShader = reinterpret_cast<ID3DBlob*>(pBlob);
        m_uHashCode = Fnv1a64(m_pCompiledShader->GetBufferPointer(), m_pCompiledShader->GetBufferSize());
        if (m_pReflector == nullptr)
        {
            m_pReflector.Attch(new KGFX_ShaderReflectorDx12);
        }
        m_pReflector->BuildReflection(pReflection, m_pShaderTechItem.m_ShageStage);

        bres = true;
    Exit0:
        return bres;
    }


    KGFX_ShaderReflectorDx12::KGFX_ShaderReflectorDx12() = default;

    KGFX_ShaderReflectorDx12::~KGFX_ShaderReflectorDx12()
    {
        Clear();
    }

    void KGFX_ShaderReflectorDx12::Clear()
    {
        m_uMtlBufferSize = 0;
        m_mapMtlUBOParamMapping.clear();
        m_mapPushConstsParamMapping.clear();
        m_vecAttribute.clear();
        m_vecUniformBlock.clear();
        m_vecUniformTexture.clear();
        m_vecUniformSampler.clear();
    }

    BOOL KGFX_ShaderReflectorDx12::ParseVertexAttribute(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        BOOL bResult = false;

        for (uint32_t i = 0; i < shader_desc.InputParameters; ++i)
        {
            RefPtr<KProgramAttribute> pAttribue(new KProgramAttribute);
            D3D12_SIGNATURE_PARAMETER_DESC paramDesc;

            pReflection->GetInputParameterDesc(i, &paramDesc);

            enumProgramDataType type = USER_TYPE;
            pAttribue->szName = GetParamNameByPool(paramDesc.SemanticName);
            assert(paramDesc.SemanticIndex == 0);
            if (paramDesc.Mask == D3D10_COMPONENT_MASK_X)
            {
                // 单通道
                if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                {
                    pAttribue->type = UINT1_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                {
                    pAttribue->type = INT1_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                {
                    pAttribue->type = FLOAT1_TYPE;
                }
                else
                {
                    KGLOG_ASSERT_EXIT(0);
                }
            }
            else if (paramDesc.Mask == (D3D10_COMPONENT_MASK_X | D3D10_COMPONENT_MASK_Y))
            {
                // 双通道
                if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                {
                    pAttribue->type = UINT2_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                {
                    pAttribue->type = INT2_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                {
                    pAttribue->type = FLOAT2_TYPE;
                }
                else
                {
                    KGLOG_ASSERT_EXIT(0);
                }
            }
            else if (paramDesc.Mask == (D3D10_COMPONENT_MASK_X | D3D10_COMPONENT_MASK_Y | D3D10_COMPONENT_MASK_Z))
            {
                // 三通道
                if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                {
                    pAttribue->type = UINT3_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                {
                    pAttribue->type = INT3_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                {
                    pAttribue->type = FLOAT3_TYPE;
                }
                else
                {
                    KGLOG_ASSERT_EXIT(0);
                }
            }
            else if (paramDesc.Mask == (D3D10_COMPONENT_MASK_X | D3D10_COMPONENT_MASK_Y | D3D10_COMPONENT_MASK_Z | D3D10_COMPONENT_MASK_W))
            {
                // 四通道
                if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
                {
                    pAttribue->type = UINT4_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
                {
                    pAttribue->type = INT4_TYPE;
                }
                else if (paramDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
                {
                    pAttribue->type = FLOAT4_TYPE;
                }
                else
                {
                    KGLOG_ASSERT_EXIT(0);
                }
            }
            else
            {
                KGLOG_ASSERT_EXIT(0);
            }

            KAttribUsage::Enum eUsage = GetKAttribUsage(pAttribue->szName);
            if (eUsage < KAttribUsage::COUNT)
            {
                pAttribue->uSize = GetProgramDataTypeSize(pAttribue->type, 1);
                pAttribue->vertexUsage = eUsage;
                if (pAttribue->vertexUsage >= KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE && pAttribue->vertexUsage <= KAttribUsage::VERT_POINT_LIGHT_INDX_INSTANCE)
                {
                    pAttribue->bInstanceData = true;
                }
                else
                {
                    pAttribue->bInstanceData = false;
                }
                pAttribue->fmt = GetVertFormat(pAttribue->type, eUsage);
                m_vecAttribute.emplace_back(std::move(pAttribue));
            }
        }
        bResult = true;
    Exit0:
        return bResult;
    }


    KProgramAttribute* KGFX_ShaderReflectorDx12::GetAttribute(const_pool_str szName) const
    {
        KProgramAttribute* pAttribute = nullptr;
        for (auto& it : m_vecAttribute)
        {
            if (it->szName == szName)
            {
                pAttribute = it;
                break;
            }
        }
        return pAttribute;
    }

    KGFX_ShaderUniformBlockDX12* KGFX_ShaderReflectorDx12::GetBufRefl(const_pool_str szName) const
    {
        auto it = std::find_if(m_vecUniformBlock.begin(), m_vecUniformBlock.end(), [szName](const RefPtr<KGFX_ShaderUniformBlockDX12>& other) {
            return other->m_szName == szName;
            });

        return it == m_vecUniformBlock.end() ? nullptr : it->Get();
    }

    KGFX_ShaderUniformTextureDX12* KGFX_ShaderReflectorDx12::GetTexRefl(const_pool_str szName) const
    {
        auto it = std::find_if(m_vecUniformTexture.begin(), m_vecUniformTexture.end(), [szName](const RefPtr<KGFX_ShaderUniformTextureDX12>& other) {
            return other->m_szName == szName;
            });

        return it == m_vecUniformTexture.end() ? nullptr : it->Get();
    }

    KGFX_ShaderUniformSamplerDX12* KGFX_ShaderReflectorDx12::GetSamplerRefl(const_pool_str szName) const
    {
        auto it = std::find_if(m_vecUniformSampler.begin(), m_vecUniformSampler.end(), [szName](const RefPtr<KGFX_ShaderUniformSamplerDX12>& other) {
            return other->m_szName == szName;
            });

        return it == m_vecUniformSampler.end() ? nullptr : it->Get();
    }

    KGFX_ShaderAccelerationStructureDX12* KGFX_ShaderReflectorDx12::GetAccelerationStructureRefl(const_pool_str szName) const
    {
        auto it = std::find_if(m_vecAccelerationStructure.begin(), m_vecAccelerationStructure.end(), [szName](const RefPtr<KGFX_ShaderAccelerationStructureDX12>& other) {
            return other->m_szName == szName;
            });

        return it == m_vecAccelerationStructure.end() ? nullptr : it->Get();
    }

    bool KGFX_ShaderReflectorDx12::ParseCBuffer(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        bool    bResult = false;
        bool    bRetCode = false;
        HRESULT hrRes = E_FAIL;

        for (uint32_t i = 0; i < shader_desc.ConstantBuffers; ++i)
        {
            ID3D12ShaderReflectionConstantBuffer* pCBuffer = pReflection->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC              cbufferDesc = {};
            hrRes = pCBuffer->GetDesc(&cbufferDesc);
            assert(hrRes == S_OK);

            /// 如果不是cbf就先跳过，由后面的srv和uav继续分析
            if (cbufferDesc.Type != D3D_CT_CBUFFER)
            {
                continue;
            }

            const_pool_str               pCbName = GetParamNameByPool(cbufferDesc.Name);
            KGFX_ShaderUniformBlockDX12* pUniformBlock = GetBufRefl(pCbName);

            if (!pUniformBlock)
            {
                bool bPerMtlUBO = false;
                bool bForPushConsts = false;
                bool bForSpeicalizationConsts = false;

                pUniformBlock = new KGFX_ShaderUniformBlockDX12;
                pUniformBlock->m_szName = pCbName;
                pUniformBlock->m_block16bytesAlignMemoryForGpu = cbufferDesc.Size;
                pUniformBlock->m_UniformType = UBO_UNIFORM;
                pUniformBlock->m_uArrayCount = 1;
                if (strcmp(pUniformBlock->m_szName, PER_MTL_UBO_NAME_0) == 0 || strcmp(pUniformBlock->m_szName, PER_MTL_UBO_NAME_1) == 0)
                {
                    bPerMtlUBO = true;
                }
                else if (strcmp(pUniformBlock->m_szName, PUSH_CONSTANTS_UBO_NAME_DX12) == 0)
                {
                    bForPushConsts = true;
                }
                else if (strcmp(pUniformBlock->m_szName, SPECIAL_CONST_NAME_DX12) == 0)
                {
                    bForSpeicalizationConsts = true;
                }

                if (bForPushConsts)
                {
                    m_uPushConstBufferSize = cbufferDesc.Size;
                    pUniformBlock->m_UniformType = PUSH_CONSTANT_UNIFORM;
                }
                else if (bForSpeicalizationConsts)
                {
                    m_uSpecializationConstBufferSize = cbufferDesc.Size;
                    pUniformBlock->m_UniformType = SPEICALIZATION_CONST_UNIFORM;
                }
                else if (bPerMtlUBO)
                {
                    m_uMtlBufferSize = cbufferDesc.Size;
                }

                for (uint32_t j = 0; j < cbufferDesc.Variables; ++j)
                {
                    ID3D12ShaderReflectionVariable* pVariable = pCBuffer->GetVariableByIndex(j);

                    D3D12_SHADER_VARIABLE_DESC variableDesc;
                    pVariable->GetDesc(&variableDesc);

                    ID3D12ShaderReflectionType* pType = pVariable->GetType();
                    D3D12_SHADER_TYPE_DESC      typeDesc;
                    hrRes = pType->GetDesc(&typeDesc);
                    assert(hrRes == S_OK);

                    if (bPerMtlUBO)
                    {
                        UBOParamItem item = {};
                        item.szName = GetParamNameByPool(variableDesc.Name);
                        item.m_uOffset = variableDesc.StartOffset;
                        item.m_uByteSize = variableDesc.Size;
                        m_mapMtlUBOParamMapping.insert(std::make_pair<>(item.szName, item));
                    }
                    else if (bForSpeicalizationConsts)
                    {
                        UBOParamItem item = {};
                        item.szName = GetParamNameByPool(variableDesc.Name);
                        item.m_uOffset = variableDesc.StartOffset;
                        item.m_uByteSize = variableDesc.Size;
                        m_mapSpecializationConstsParamMapping.insert(std::make_pair<>(item.szName, item));
                    }
                    else if (bForPushConsts)
                    {
                        UBOParamItem item = {};
                        item.szName = GetParamNameByPool(variableDesc.Name);
                        item.m_uOffset = variableDesc.StartOffset;
                        item.m_uByteSize = variableDesc.Size;
                        m_mapPushConstsParamMapping.insert(std::make_pair<>(item.szName, item));
                    }
                }

                m_vecUniformBlock.emplace_back(pUniformBlock);
            }

            KGLOG_PROCESS_ERROR(pUniformBlock);

            for (uint32_t k = 0; k < shader_desc.BoundResources; ++k)
            {
                D3D12_SHADER_INPUT_BIND_DESC bindDesc;
                hrRes = pReflection->GetResourceBindingDesc(k, &bindDesc);
                assert(hrRes == S_OK);

                const_pool_str pBindName = GetParamNameByPool(bindDesc.Name);
                if (pBindName == pCbName)
                {
                    KGFX_ShaderUniformBlockDX12* pUniformBlockUbo = GetBufRefl(pCbName);
                    assert(pUniformBlockUbo);
                    if (pUniformBlockUbo)
                    {
                        pUniformBlockUbo->m_CBufBinds = ShaderOffset{ static_cast<uint16_t>(bindDesc.Space), static_cast<uint16_t>(bindDesc.BindPoint) };
                    }
                }
            }
        }

        bRetCode = true;
    Exit0:
        return bRetCode;
    }

    bool KGFX_ShaderReflectorDx12::ParseUAV(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        bool    bResult = false;
        HRESULT hrRes = E_FAIL;

        for (uint32_t i = 0; i < shader_desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            hrRes = pReflection->GetResourceBindingDesc(i, &bindDesc);
            assert(hrRes == S_OK);
            const_pool_str pName = GetParamNameByPool(bindDesc.Name);

            /// rwstructureBuf
            if (bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_RWTYPED ||
                bindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS ||
                bindDesc.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                bindDesc.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER ||
                bindDesc.Type == D3D_SIT_UAV_FEEDBACKTEXTURE)
            {
                KGFX_ShaderUniformBlockDX12* pUniformBlock = GetBufRefl(pName);
                if (!pUniformBlock)
                {
                    pUniformBlock = new KGFX_ShaderUniformBlockDX12;
                    pUniformBlock->m_szName = pName;
                    pUniformBlock->m_UniformType = SSBO_UNIFORM;
                    pUniformBlock->m_uArrayCount = bindDesc.BindCount;
                    m_vecUniformBlock.emplace_back(pUniformBlock);
                }
                else
                {
                    pUniformBlock->m_UniformType = SSBO_UNIFORM;
                }
                KGLOG_PROCESS_ERROR(pUniformBlock);

                pUniformBlock->m_CBufBinds = ShaderOffset{ static_cast<uint16_t>(bindDesc.Space), static_cast<uint16_t>(bindDesc.BindPoint) };
            }
        }

        bResult = true;
    Exit0:
        return bResult;
    }

    bool KGFX_ShaderReflectorDx12::ParseSampler(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        BOOL    bResult = false;
        BOOL    bRetCode = false;
        HRESULT hrRes = E_FAIL;

        for (uint32_t i = 0; i < shader_desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            hrRes = pReflection->GetResourceBindingDesc(i, &bindDesc);
            assert(hrRes == S_OK);

            if (bindDesc.Type == D3D_SIT_SAMPLER)
            {
                const_pool_str                 pName = GetParamNameByPool(bindDesc.Name);
                KGFX_ShaderUniformSamplerDX12* pSampler = GetSamplerRefl(pName);
                if (!pSampler)
                {
                    pSampler = new KGFX_ShaderUniformSamplerDX12;
                    pSampler->m_szName = pName;
                    m_vecUniformSampler.emplace_back(pSampler);
                }

                KGLOG_PROCESS_ERROR(pSampler);
                pSampler->m_SamplerBinds = ShaderOffset{ static_cast<uint16_t>(bindDesc.Space), static_cast<uint16_t>(bindDesc.BindPoint) };
            }
        }
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KGFX_ShaderReflectorDx12::ParseAccelerationStructure(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        BOOL    bResult = false;
        BOOL    bRetCode = false;
        HRESULT hrRes = E_FAIL;

        for (uint32_t i = 0; i < shader_desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            hrRes = pReflection->GetResourceBindingDesc(i, &bindDesc);
            assert(hrRes == S_OK);

            if (bindDesc.Type == D3D_SIT_RTACCELERATIONSTRUCTURE)
            {
                const_pool_str                 pName = GetParamNameByPool(bindDesc.Name);
                KGFX_ShaderAccelerationStructureDX12* pAccelerationStructure = GetAccelerationStructureRefl(pName);
                if (!pAccelerationStructure)
                {
                    pAccelerationStructure = new KGFX_ShaderAccelerationStructureDX12;
                    pAccelerationStructure->m_szName = pName;
                    m_vecAccelerationStructure.emplace_back(pAccelerationStructure);
                }

                KGLOG_PROCESS_ERROR(pAccelerationStructure);
                pAccelerationStructure->m_AccelerationStructureBinds = ShaderOffset{ static_cast<uint16_t>(bindDesc.Space), static_cast<uint16_t>(bindDesc.BindPoint) };
            }
        }
        bResult = true;
    Exit0:
        return bResult;
    }

    bool KGFX_ShaderReflectorDx12::ParseTexture(ID3D12ShaderReflection* pReflection, const D3D12_SHADER_DESC& shader_desc)
    {
        bool    bResult = false;
        bool    bRetCode = false;
        HRESULT hrRes = E_FAIL;

        for (uint32_t i = 0; i < shader_desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC bindDesc;
            hrRes = pReflection->GetResourceBindingDesc(i, &bindDesc);
            assert(hrRes == S_OK);

            if (bindDesc.Type == D3D_SIT_TEXTURE ||
                bindDesc.Type == D3D_SIT_TBUFFER ||
                bindDesc.Type == D3D_SIT_STRUCTURED ||
                bindDesc.Type == D3D_SIT_BYTEADDRESS)
            {
                const_pool_str                 pName = GetParamNameByPool(bindDesc.Name);
                KGFX_ShaderUniformTextureDX12* pTex = GetTexRefl(pName);
                if (!pTex)
                {
                    pTex = new KGFX_ShaderUniformTextureDX12;
                    pTex->m_szName = pName;
                    pTex->m_UniformType = TEXTURE_UNIFORM;
                    pTex->m_uArrayCount = bindDesc.BindCount;
                    m_vecUniformTexture.emplace_back(pTex);
                }
                else
                {
                    pTex->m_UniformType = TEXTURE_UNIFORM;
                }
                KGLOG_PROCESS_ERROR(pTex);

                pTex->m_TextureBinds = ShaderOffset{ static_cast<uint16_t>(bindDesc.Space), static_cast<uint16_t>(bindDesc.BindPoint) };
            }
        }

        bRetCode = true;
    Exit0:
        return bRetCode;
    }

    BOOL KGFX_ShaderReflectorDx12::BuildReflection(void* pProgram, ShaderStageType shaderType)
    {
        BOOL                    bResult = false;
        BOOL                    bRetCode = false;
        ID3D12ShaderReflection* pReflection = static_cast<ID3D12ShaderReflection*>(pProgram);

        D3D12_SHADER_DESC shader_desc;
        pReflection->GetDesc(&shader_desc);

        if (shaderType == ShaderStageType::Vertex)
        {
            bRetCode = ParseVertexAttribute(pReflection, shader_desc);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        bRetCode = ParseCBuffer(pReflection, shader_desc);
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = ParseUAV(pReflection, shader_desc);
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = ParseSampler(pReflection, shader_desc);
        KGLOG_PROCESS_ERROR(bRetCode);

        bRetCode = ParseTexture(pReflection, shader_desc);
        KGLOG_PROCESS_ERROR(bRetCode);

        //bRetCode = ParseAccelerationStructure(pReflection, shader_desc);
        //KGLOG_PROCESS_ERROR(bRetCode);

        bResult = true;
    Exit0:
        SAFE_RELEASE(pReflection);
        return bResult;
    }

    ////////////////////////////////////////////////////////////
    KGFX_ShaderResourcePoolDx12::KGFX_ShaderResourcePoolDx12() = default;

    KGFX_ShaderResourcePoolDx12::~KGFX_ShaderResourcePoolDx12()
    {
        {
            std::lock_guard lock(m_shaderLock);
            if (!m_mapShader.empty())
            {
                KGLogPrintf(KGLOG_ERR, "%s", "还有未被删除的shader，应该是有泄露了");
                for (auto& it : m_mapShader)
                {
                    KGFX_ShaderDx12* pShader = it.second;
                    KGLogPrintf(KGLOG_ERR, "强行删除%s", pShader->GetKey());
                    SAFE_DELETE(pShader);
                }
                m_mapShader.clear();
            }
        }
    }

    /**
     * 解析整个json 并读取解析出来的shader文件 将文件发往DXC编译 组装编译结果和反射
     * @param szTechFilePathName
     * @param szTechName
     * @param sUserShaderLoc
     * @param szUserDefMacro
     * @param pReflector
     * @return
     */
    bool KGFX_ShaderResourcePoolDx12::RequestFromTechFileDXC(const char* szTechFilePathName, const char* szTechName, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro, KGFX_ProgramReflectorDx12** pReflector)
    {
        bool bRetCode = false;

        std::filesystem::path shaderTechPath = szTechFilePathName;
        assert(shaderTechPath.filename().extension() == ".jsontech");

        std::string techHash = GetTechKey(szTechFilePathName, szTechName, sUserShaderLoc, szUserDefMacro);

        std::lock_guard lock(m_shaderLock);
        auto            techMapit = m_mapTechRefl.find(techHash);
        if (techMapit != m_mapTechRefl.end())
        {
            if (techMapit->second)
            {
                *pReflector = techMapit->second;
                (*pReflector)->AddRef();
            }
            else
            {
                assert(false);
            }
        }
        else
        {
            std::string szWholeFileString;
            KUniqueStr  ustrShaderPath = g_CachePathString(shaderTechPath.string().c_str(), TRUE);

            NSKBase::tagFileLocation sShaderLoc(ustrShaderPath);
            bRetCode = IncludeFileHelper::ReadWholeShaderFile(sShaderLoc, szWholeFileString, nullptr);
            KGLOG_PROCESS_ERROR(bRetCode);

            std::vector<KGFX_ShaderDx12*> allStageShader = {};

            // 尝试用json去解析
            rapidjson::Document JsonDocument;
            JsonDocument.Parse(szWholeFileString.c_str());
            KGLOG_ASSERT_EXIT(!JsonDocument.HasParseError());
            auto& ParamObjectArray = JsonDocument["Tech"];
            ASSERT(ParamObjectArray.IsArray());
            bool bFind = false;


            for (auto it = ParamObjectArray.Begin(), iend = ParamObjectArray.End(); it != iend; ++it)
            {
                ASSERT(it->IsObject());
                auto        ParamObject = it->GetObject();
                const char* pTechName = ParamObject["Name"].GetString();
                if (strcmp(pTechName, szTechName) == 0)
                {
                    auto& programArray = ParamObject["Program"];
                    ASSERT(programArray.IsArray());

                    KGFX_ShaderDx12* pShader = nullptr;
                    for (auto itt = programArray.Begin(), end = programArray.End(); itt != end; ++itt)
                    {
                        ASSERT(itt->IsObject());
                        auto            programItem = itt->GetObject();
                        ShaderStageType ShaderStage = {};
                        bFind = true;
                        const char* pShaderType = programItem["ShaderType"].GetString();
                        const char* pEntrypoint = programItem["Entrypoint"].GetString();
                        const char* pMacroDefine = programItem["MacroDefine"].GetString();
                        const char* pShaderPath = programItem["Path"].GetString();
                        assert(pShaderType);
                        assert(pEntrypoint);
                        assert(pShaderPath);

                        if (strcmp(pShaderType, "vs") == 0)
                        {
                            ShaderStage = ShaderStageType::Vertex;
                        }
                        else if (strcmp(pShaderType, "hs") == 0)
                        {
                            ShaderStage = ShaderStageType::Geometry;
                        }
                        else if (strcmp(pShaderType, "ds") == 0)
                        {
                            ShaderStage = ShaderStageType::Geometry;
                        }
                        else if (strcmp(pShaderType, "gs") == 0)
                        {
                            ShaderStage = ShaderStageType::Geometry;
                        }
                        else if (strcmp(pShaderType, "fs") == 0)
                        {
                            ShaderStage = ShaderStageType::Fragment;
                        }
                        else if (strcmp(pShaderType, "cs") == 0)
                        {
                            ShaderStage = ShaderStageType::Compute;
                        }
                        else
                        {
                            /// 写错了检查自己的tech
                            assert(false);
                        }

                        std::string key = GetShaderKey(ShaderStage, pShaderPath, pEntrypoint, sUserShaderLoc, szUserDefMacro, pMacroDefine);

                        auto shaderMapit = m_mapShader.find(key);
                        if (shaderMapit == m_mapShader.end())
                        {
                            pShader = new KGFX_ShaderDx12;
                            pShader->SetKey(key.c_str());
                            bRetCode = pShader->LoadShaderDXC(ShaderStage, key.c_str(), pShaderPath, pEntrypoint, sUserShaderLoc.GetFilePath(), szUserDefMacro, pMacroDefine);
                            if (bRetCode)
                            {
                                m_mapShader.insert(std::make_pair<>(key.c_str(), pShader));
                            }
                            else
                            {
                                assert(false);
                                SAFE_DELETE(pShader);
                                break;
                            }
                        }
                        else
                        {
                            pShader = shaderMapit->second;
                            pShader->AddRef();
                        }

                        allStageShader.emplace_back(pShader);
                    }

                    break;
                }
            }
            assert(bFind);
            KGLOG_PROCESS_ERROR(bFind);
            if (bFind)
            {
                KGFX_ProgramReflectorDx12* programReflectorDx12 = new KGFX_ProgramReflectorDx12;
                programReflectorDx12->SetKey(techHash);
                programReflectorDx12->Init(allStageShader);
                m_mapTechRefl.insert(std::make_pair<>(techHash, programReflectorDx12));
                *pReflector = programReflectorDx12;
            }
        }

        bRetCode = true;
    Exit0:
        return bRetCode;
    }

    std::string KGFX_ShaderResourcePoolDx12::GetShaderKey(ShaderStageType eShaderStage, const char* szShaderFilePath, const char* szEntryPointName, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro, const char* szFileDefMacro) const
    {
        std::string key = szShaderFilePath;
        auto        szShaderTypeName = IncludeFileHelper::GetShaderTypeName(eShaderStage);
        key.append("@");

        if (szEntryPointName && szEntryPointName[0])
        {
            key.append(szEntryPointName);
            key.append("@");
        }

        key.append(szShaderTypeName);

        if (sUserShaderLoc.IsValid())
        {
            key.append("@");
            key.append(sUserShaderLoc.GetFilePath().Str());
        }

        if (szUserDefMacro && szUserDefMacro[0])
        {
            key.append("@");
            key.append(szUserDefMacro);
        }

        if (szFileDefMacro && szFileDefMacro[0])
        {
            key.append("@");
            key.append(szFileDefMacro);
        }
        return key;
    }

    std::string KGFX_ShaderResourcePoolDx12::GetTechKey(const char* szTechFilePathName, const char* szTechName, const NSKBase::tagFileLocation& sUserShaderLoc, const char* szUserDefMacro) const
    {
        std::string techHash = {};

        if (szTechFilePathName && szTechFilePathName[0])
        {
            techHash.append(szTechFilePathName);
            techHash.append("@");
        }

        if (szTechName && szTechName[0])
        {
            techHash.append(szTechName);
            techHash.append("@");
        }

        if (sUserShaderLoc.GetFilePath())
        {
            techHash.append(sUserShaderLoc.GetFilePath());
            techHash.append("@");
        }

        if (szUserDefMacro && szUserDefMacro[0])
        {
            techHash.append(szUserDefMacro);
            techHash.append("@");
        }
        return techHash;
    }

    BOOL KGFX_ShaderResourcePoolDx12::RemoveShader(const std::string& pKey)
    {
        std::lock_guard lock(m_shaderLock);
        BOOL            bRetCode = false;
        auto            it = m_mapShader.find(pKey);
        if (it != m_mapShader.end())
        {
            KGFX_ShaderDx12* pShader = it->second;
            if (pShader->GetRef() == 0)
            {
                SAFE_DELETE(pShader);
                m_mapShader.erase(it);
                bRetCode = true;
            }
        }
        return bRetCode;
    }

    BOOL KGFX_ShaderResourcePoolDx12::RemoveProgram(const std::string& pKey)
    {
        std::lock_guard lock(m_PoolLock);
        BOOL            bRetCode = false;
        auto            it = m_mapTechRefl.find(pKey);
        if (it != m_mapTechRefl.end())
        {
            KGFX_ProgramReflectorDx12* pReflector = it->second;
            if (pReflector && pReflector->GetRef() == 0)
            {
                //SAFE_DELETE(pReflector);
                m_mapTechRefl.erase(it);
                bRetCode = true;
            }
        }
        return bRetCode;
    }

    ////////////////////////////////////////////////////////////


    KGFX_ProgramReflectorDx12::~KGFX_ProgramReflectorDx12()
    {
        Reset();
    }

    int32_t KGFX_ProgramReflectorDx12::Release()
    {
        int32_t nRef = --m_nRef;
        if (nRef == 0)
        {
            KGFX_ShaderResourcePoolDx12* pPool = KGFX_GetShaderPoolDx12();
            BOOL                         bRet = pPool->RemoveProgram(m_szKey);
            ASSERT(bRet);

            if (m_RootSignature)
            {
                auto piDevice = KGFX_GetGraphicDeviceDx12Internal();
                CHECK_ASSERT(piDevice);

                piDevice->GC_DelayReleaseObject(this);
            }
            else
            {
                // 如果没有初始化成功，直接释放
                delete this;
            }
        }
        return nRef;
    }

    void KGFX_ProgramReflectorDx12::Init(std::vector<KGFX_ShaderDx12*>& vecShaderStages)
    {
        m_vecShaderStages = std::move(vecShaderStages);
        CombineAllStageRefl();
    }

    void KGFX_ProgramReflectorDx12::Reset()
    {
        SAFE_RELEASE(m_pMtlCbuf);
        SAFE_RELEASE(m_RootSignature);

        for (auto& stage : m_vecShaderStages)
        {
            SAFE_RELEASE(stage);
        }
        m_vecUniformBlock.clear();
        m_vecUniformTexture.clear();
        m_vecUniformSampler.clear();
        m_SpecialConstRefl = {};
        m_MtlConst = {};
        m_ShaderNameToSlot = {};
        m_ShaderStageSRVCount = {};
        m_ShaderStageUAVCount = {};
        m_ShaderStageSampleCount = {};
        m_ShaderStageSRVBaseIndex = {};
        m_ShaderStageUAVBaseIndex = {};
        m_ShaderStageSamplerIndex = {};
        m_ShaderRootCBV.clear();
        m_ShaderRootConstBufferCount = 0;
    }

    void KGFX_ProgramReflectorDx12::SetKey(std::string_view szkey)
    {
        assert(m_szKey.empty());
        m_szKey = szkey;
    }

    const std::string& KGFX_ProgramReflectorDx12::GetKey()
    {
        return m_szKey;
    }

    bool KGFX_ProgramReflectorDx12::CombineAllStageRefl()
    {
        /// 合并多个stage的反射
        for (int i = 0; i < m_vecShaderStages.size(); i++)
        {
            auto shaderStage = m_vecShaderStages.at(i)->GetShaderStage();
            {
                auto& pCBUFS = m_vecShaderStages.at(i)->m_pReflector->m_vecUniformBlock;
                /// 合并UBO的反射
                for (auto& ubo : pCBUFS)
                {
                    assert(ubo);
                    auto name = ubo->m_szName;
                    auto UBOOffset = ubo->m_CBufBinds;
                    auto it = std::find_if(m_vecUniformBlock.begin(), m_vecUniformBlock.end(), [name](const KGFX_ProgramUniformBlockDX12& other) {
                        return other.m_pCBVStageRefl && (name == other.m_pCBVStageRefl->m_szName);
                        });

                    if (it != m_vecUniformBlock.end())
                    {
                        it->m_CBufBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = UBOOffset;
                    }
                    else
                    {
                        KGFX_ProgramUniformBlockDX12 newCbuf = {};
                        newCbuf.m_pCBVStageRefl = ubo;
                        newCbuf.m_CBufBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = UBOOffset;

                        m_vecUniformBlock.emplace_back(newCbuf);
                    }
                }
            }

            {
                auto& pRESS = m_vecShaderStages.at(i)->m_pReflector->m_vecUniformTexture;
                /// 合并RES的反射
                for (auto& res : pRESS)
                {
                    assert(res);
                    auto name = res->m_szName;
                    auto ResOffset = res->m_TextureBinds;
                    auto it = std::find_if(m_vecUniformTexture.begin(), m_vecUniformTexture.end(), [name](const KGFX_ProgramUniformTextureDX12& other) {
                        return other.m_pResStageRefl && name == other.m_pResStageRefl->m_szName;
                        });

                    if (it != m_vecUniformTexture.end())
                    {
                        it->m_TexBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = ResOffset;
                    }
                    else
                    {
                        KGFX_ProgramUniformTextureDX12 newRes = {};
                        newRes.m_pResStageRefl = res;
                        newRes.m_TexBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = ResOffset;

                        m_vecUniformTexture.emplace_back(newRes);
                    }
                }
            }

            {
                auto& pSamplerS = m_vecShaderStages.at(i)->m_pReflector->m_vecUniformSampler;
                /// 合并RES的反射
                for (auto& sampler : pSamplerS)
                {
                    assert(sampler);
                    auto name = sampler->m_szName;
                    auto SamplerOffset = sampler->m_SamplerBinds;
                    auto it = std::find_if(m_vecUniformSampler.begin(), m_vecUniformSampler.end(), [name](const KGFX_ProgramUniformSamplerDX12& other) {
                        return other.m_pSamplerStageRefl && name == other.m_pSamplerStageRefl->m_szName;
                        });

                    if (it != m_vecUniformSampler.end())
                    {
                        it->m_SamplerBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = SamplerOffset;
                    }
                    else
                    {
                        KGFX_ProgramUniformSamplerDX12 newSampler = {};
                        newSampler.m_pSamplerStageRefl = sampler;
                        newSampler.m_SamplerBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = SamplerOffset;

                        m_vecUniformSampler.emplace_back(newSampler);
                    }
                }

                auto& pAccelerationStructure = m_vecShaderStages.at(i)->m_pReflector->m_vecAccelerationStructure;
                for (auto& AccelerationStructure : pAccelerationStructure)
                {
                    assert(AccelerationStructure);
                    auto name = AccelerationStructure->m_szName;
                    auto BindOffset = AccelerationStructure->m_AccelerationStructureBinds;
                    auto it = std::find_if(m_vecUniformAccelerationStructure.begin(), m_vecUniformAccelerationStructure.end(), [name](const KGFX_ProgramUniformAccelerationStructureDX12& other) {
                        return other.m_pAccelerationStructureStageRefl && name == other.m_pAccelerationStructureStageRefl->m_szName;
                        });

                    if (it != m_vecUniformAccelerationStructure.end())
                    {
                        it->m_AccelerationStructureBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = BindOffset;
                    }
                    else
                    {
                        KGFX_ProgramUniformAccelerationStructureDX12 newAccelerationStructure = {};
                        newAccelerationStructure.m_pAccelerationStructureStageRefl = AccelerationStructure;
                        newAccelerationStructure.m_AccelerationStructureBinds.at(gfx::CalculateShaderStageTypeIndex(shaderStage)) = BindOffset;

                        m_vecUniformAccelerationStructure.emplace_back(newAccelerationStructure);
                    }
                }
            }
        }

        {
            uint32_t mtlBufSize = 0;
            /// 检测mtl cbuf的匹配并创建
            for (int i = 0; i < m_vecShaderStages.size(); i++)
            {
                if (!m_vecShaderStages.at(i)->m_pReflector->m_mapMtlUBOParamMapping.empty())
                {
                    m_MtlConst.m_MtlConstBinds = &m_vecShaderStages.at(i)->m_pReflector->m_mapMtlUBOParamMapping;
                    if (mtlBufSize == 0)
                    {
                        mtlBufSize = m_vecShaderStages.at(i)->m_pReflector->m_uMtlBufferSize;
                    }
                    else
                    {
                        assert(mtlBufSize == m_vecShaderStages.at(i)->m_pReflector->m_uMtlBufferSize);
                    }
                }
            }

            if (mtlBufSize && !m_pMtlCbuf)
            {
                m_pMtlCbuf = new KGFX_MemoryConstBuffer;
                m_pMtlCbuf->Init(mtlBufSize);
            }
        }

        {
            uint32_t pushConstItemSize = 0;
            uint32_t specialConstItemSize = 0;
            /// 检测push const cbuf的匹配
            for (int i = 0; i < m_vecShaderStages.size(); i++)
            {
                if (!m_vecShaderStages.at(i)->m_pReflector->m_mapPushConstsParamMapping.empty())
                {
                    if (pushConstItemSize == 0)
                    {
                        pushConstItemSize = m_vecShaderStages.at(i)->m_pReflector->m_uPushConstBufferSize;
                    }
                    else
                    {
                        assert(pushConstItemSize == m_vecShaderStages.at(i)->m_pReflector->m_uPushConstBufferSize);
                    }
                }



                if (!m_vecShaderStages.at(i)->m_pReflector->m_mapSpecializationConstsParamMapping.empty())
                {
                    if (specialConstItemSize == 0)
                    {
                        specialConstItemSize = m_vecShaderStages.at(i)->m_pReflector->m_uSpecializationConstBufferSize;
                        m_SpecialConstRefl.m_SpecialConstBinds = &m_vecShaderStages.at(i)->m_pReflector->m_mapSpecializationConstsParamMapping;
                        m_SpecialConstRefl.m_SpecialConstSize = specialConstItemSize;
                    }
                    else
                    {
                        assert(specialConstItemSize == m_vecShaderStages.at(i)->m_pReflector->m_uSpecializationConstBufferSize);
                    }
                }

            }
            m_PushConst.m_uPushConstBufferSize = pushConstItemSize;
        }
        return true;
    }

    const std::vector<KGFX_ShaderDx12*>& KGFX_ProgramReflectorDx12::GetShaderCode()
    {
        return m_vecShaderStages;
    }

    std::vector<KGFX_ProgramUniformBlockDX12>& KGFX_ProgramReflectorDx12::GetAllBufRefl()
    {
        return m_vecUniformBlock;
    }

    std::vector<KGFX_ProgramUniformTextureDX12>& KGFX_ProgramReflectorDx12::GetAllResRefl()
    {
        return m_vecUniformTexture;
    }

    std::vector<KGFX_ProgramUniformSamplerDX12>& KGFX_ProgramReflectorDx12::GetAllSamplerRefl()
    {
        return m_vecUniformSampler;
    }

    std::vector< KGFX_ProgramUniformAccelerationStructureDX12>& KGFX_ProgramReflectorDx12::GetAllAccelerationStructureRefl()
    {
        return m_vecUniformAccelerationStructure;
    }

    const KGFX_ProgramSpecialConstDX12& KGFX_ProgramReflectorDx12::GetSpecialConstRefl() const
    {
        return m_SpecialConstRefl;
    }

    const KGFX_ProgramMtlConstDX12& KGFX_ProgramReflectorDx12::GetMtlConstRefl() const
    {
        return m_MtlConst;
    }

    IKGFX_ConstBuffer* KGFX_ProgramReflectorDx12::GetMtlCbuf() const
    {
        return m_pMtlCbuf;
    }


    ID3D12RootSignature* KGFX_ProgramReflectorDx12::GetRootSignature() const
    {
        return m_RootSignature;
    }

    uint64_t KGFX_ProgramReflectorDx12::GetRootSignatureHash() const
    {
        return m_uRootSignatureHash;
    }

    bool KGFX_ProgramReflectorDx12::SetPerMtlValue(const char* name, const void* data, uint32_t size)
    {
        if (m_pMtlCbuf)
        {
            if (m_MtlConst.m_MtlConstBinds && m_pMtlCbuf)
            {
                auto it = m_MtlConst.m_MtlConstBinds->find(name);
                if (it != m_MtlConst.m_MtlConstBinds->end())
                {
                    uint32_t offset = it->second.m_uOffset;
                    uint8_t* pData = m_pMtlCbuf->GetCpuData();
                    if (memcmp(pData + offset, data, size) != 0)
                    {
                        memcpy(pData + offset, data, size);
                        m_bMtlCbufNeedUpdate = true;
                    }
                }
            }
        }

        return true;
    }

    static D3D12_SHADER_VISIBILITY IndexToShaderVisib(int index)
    {
        switch (index)
        {
        case 0:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case 1:
            return D3D12_SHADER_VISIBILITY_HULL;
        case 2:
            return D3D12_SHADER_VISIBILITY_DOMAIN;
        case 3:
            return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case 4:
            return D3D12_SHADER_VISIBILITY_PIXEL;
        default:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    static ShaderResourceVisible FillShaderVisible(int shaderStage, const ShaderOffset& binds, KGfxResourceViewType viewType)
    {
        ShaderResourceVisible shaderVisible = {};
        shaderVisible.stageType = static_cast<ShaderStageType>(1 << shaderStage);
        shaderVisible.slot.bindingSlotIndex = binds.bindingSlotIndex;
        shaderVisible.slot.bindingSpaceIndex = binds.bindingSpaceIndex;
        shaderVisible.slotType = (viewType);
        return shaderVisible;
    }

    bool KGFX_ProgramReflectorDx12::BuildRootSignature()
    {
        HRESULT                 hResult = E_FAIL;
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        if (m_RootSignature == nullptr)
        {
            uint32_t uRootSignatureSize = 0;

            for (auto& gUnifromTypeName : m_ShaderNameToSlot)
            {
                gUnifromTypeName.clear();
            }
            std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> SRVCountReflect = {};
            std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> UAVCountReflect = {};
            std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> SamplerCountReflect = {};
            std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()> AccelerationStructureCountReflect = {};

            /// 这个是记录SRV和UAV的槽位的起始索引，有些时候分配的槽位起始不是0
            std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> SRVBaseIndexReflect = {};
            SRVBaseIndexReflect.fill(INVALID_USLOT);
            std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> UAVBaseIndexReflect = {};
            UAVBaseIndexReflect.fill(INVALID_USLOT);
            std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()> SampleBaseIndexReflect = {};
            SampleBaseIndexReflect.fill(INVALID_USLOT);
            std::array<bool, CalculateGraphicsAndComputeShaderStageTypeCount()> hasDescriptorStage = {};

            std::vector<CD3DX12_ROOT_PARAMETER> vecParam = {};
            vecParam.reserve(6);
            std::vector<CD3DX12_DESCRIPTOR_RANGE> vecAllRangeTable = {};
            vecAllRangeTable.reserve(6);

            auto insertFun = [this](const char* name, uint8_t value)
                {
                    auto findRes = m_ShaderNameToCPUBind.find(name);
                    if (findRes != m_ShaderNameToCPUBind.end())
                    {
                        findRes->second[findRes->second[7]++] = value;
                    }
                    else
                    {
                        BindIndex temp = {};
                        temp[0] = value;
                        temp[7] = 1;
                        m_ShaderNameToCPUBind[name] = temp;
                    }

                };

            auto insertSamplerFun = [this](const char* name, uint8_t value)
                {
                    auto findRes = m_ShaderNameToCPUSampler.find(name);
                    if (findRes != m_ShaderNameToCPUSampler.end())
                    {
                        findRes->second[findRes->second[7]++] = value;
                    }
                    else
                    {
                        BindIndex temp = {};
                        temp[0] = value;
                        temp[7] = 1;
                        m_ShaderNameToCPUSampler[name] = temp;
                    }
                };

            auto insertCbufFun = [this](const char* name, uint8_t value)
                {
                    auto findRes = m_ShaderRootCBV.find(name);
                    if (findRes != m_ShaderRootCBV.end())
                    {
                        findRes->second[findRes->second[7]++] = value;
                    }
                    else
                    {
                        BindIndex temp = {};
                        temp[0] = value;
                        temp[6] = BindIndex::Normal;
                        if (strcmp(name, SPECIAL_CONST_NAME_DX12) == 0)
                        {
                            temp[6] = BindIndex::SpecialConst;
                        }
                        else if (strcmp(name, PUSH_CONSTANTS_UBO_NAME_DX12) == 0)
                        {
                            temp[6] = BindIndex::PushConst;
                        }

                        temp[7] = 1;
                        m_ShaderRootCBV[name] = temp;
                    }
                };

            /// 处理所有的buffer
            ///	1.cbuf 2.srvBuf 3.uav 4.pushConstBuf
            /// srvBuf和srv tex放到下面去，一起加到table
            /// uavBuf和uav tex放到下面去，一起加到table
            for (auto it : GetAllBufRefl())
            {
                KGFX_ProgramUniformBlockDX12* pBlock = &it;

                /// UBO不可能是数组，只能是一个
                if (pBlock->GetType() == UBO_UNIFORM) // cbuffer
                {
                    for (int i = 0; i < pBlock->m_CBufBinds.size(); i++)
                    {
                        auto& binds = pBlock->m_CBufBinds.at(i);

                        if (binds.IsValid())
                        {
                            assert(binds.bindingSlotIndex < PUSH_CONST_REGISTER_DX12);
                            ShaderResourceVisible  shaderVisible = FillShaderVisible(i, binds, KGfxResourceViewType::RESOURCE_VIEW_TYPE_CBV);
                            CD3DX12_ROOT_PARAMETER param = {};
                            //m_ShaderRootCBV.insert({ pBlock->GetName(), static_cast<uint32_t>(vecParam.size()) });
                            std::invoke(insertCbufFun, pBlock->GetName(), static_cast<uint32_t>(vecParam.size()));

                            auto shaderStage = IndexToShaderVisib(i);
                            /// 由于RootConstantBufferView的shader可见是不能进行或运算的，所以即使是多个shader都需要这个buffer，也必须添加多次
                            param.InitAsConstantBufferView(binds.bindingSlotIndex, binds.bindingSpaceIndex, shaderStage);
                            vecParam.emplace_back(param);
                            uRootSignatureSize += 2;

                            std::invoke(insertFun, pBlock->GetName(), 0);
                            //m_ShaderNameToCPUBind.insert({ pBlock->GetName(), 0 });
                            m_ShaderNameToSlot.at(i).insert({ pBlock->GetName(), shaderVisible });
                            hasDescriptorStage.at(i) = true;
                        }
                    }
                }
            }

            std::for_each(m_ShaderNameToSlot.begin(), m_ShaderNameToSlot.end(), [this](const ShaderNameToSlot& o) {
                m_CbufCount += (uint32_t)o.size();
                });

            for (auto it : GetAllBufRefl())
            {
                KGFX_ProgramUniformBlockDX12* pBlock = &it;
                if (pBlock->GetType() == PUSH_CONSTANT_UNIFORM) // cbuffer
                {

                    for (int i = 0; i < pBlock->m_CBufBinds.size(); i++)
                    {
                        auto& binds = pBlock->m_CBufBinds.at(i);
                        if (binds.IsValid())
                        {
                            assert(binds.bindingSlotIndex == PUSH_CONST_REGISTER_DX12);
                            hasDescriptorStage.at(i) = true;
                        }
                    }

                    CD3DX12_ROOT_PARAMETER param = {};
                    std::invoke(insertCbufFun, pBlock->GetName(), static_cast<uint32_t>(vecParam.size()));
                    param.InitAsConstants(pBlock->GetBufSize() / 4, PUSH_CONST_REGISTER_DX12, 0);
                    vecParam.emplace_back(param);
                    uRootSignatureSize += (pBlock->GetBufSize() / 4);

                }

                if (pBlock->GetType() == SPEICALIZATION_CONST_UNIFORM) // cbuffer
                {

                    for (int i = 0; i < pBlock->m_CBufBinds.size(); i++)
                    {
                        auto& binds = pBlock->m_CBufBinds.at(i);
                        if (binds.IsValid())
                        {
                            assert(binds.bindingSlotIndex == SPECIAL_CONST_REGISTER_DX12);
                            hasDescriptorStage.at(i) = true;
                        }
                    }

                    CD3DX12_ROOT_PARAMETER param = {};
                    std::invoke(insertCbufFun, pBlock->GetName(), static_cast<uint32_t>(vecParam.size()));
                    param.InitAsConstants(pBlock->GetBufSize() / 4, SPECIAL_CONST_REGISTER_DX12, 0);
                    vecParam.emplace_back(param);
                    uRootSignatureSize += (pBlock->GetBufSize() / 4);
                }
            }


            /// 统计所有的贴图（为了保证槽位的连续，buffer也需要和tex放在一起）
            ///	1.srv 
            for (auto it : GetAllResRefl())
            {
                KGFX_ProgramUniformTextureDX12* pTex = &it;
                if (pTex->GetType() == TEXTURE_UNIFORM)
                {
                    uint32_t BindSlotCount = pTex->GetTexArrSize();

                    for (int i = 0; i < pTex->m_TexBinds.size(); i++)
                    {
                        auto& binds = pTex->m_TexBinds.at(i);

                        if (binds.IsValid())
                        {
                            ShaderResourceVisible shaderVisible = FillShaderVisible(i, binds, KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
                            hasDescriptorStage.at(i) = true;
                            m_ShaderNameToSlot.at(i).insert({ pTex->GetName(), shaderVisible });
                            SRVBaseIndexReflect.at(i) = std::min(shaderVisible.slot.bindingSlotIndex, SRVBaseIndexReflect.at(i));
                            SRVCountReflect.at(i) += BindSlotCount;
                        }
                    }
                }
            }



            std::for_each(SRVCountReflect.begin(), SRVCountReflect.end(), [this](uint32_t count) {                m_SRVCount += count;
                });



            //accelerationstructure
            for (auto it : GetAllAccelerationStructureRefl())
            {
                KGFX_ProgramUniformAccelerationStructureDX12* pAccelerationStructure = &it;

                for (int i = 0; i < pAccelerationStructure->m_AccelerationStructureBinds.size(); i++)
                {
                    auto binds = pAccelerationStructure->m_AccelerationStructureBinds.at(i);

                    if (binds.IsValid())
                    {
                        ShaderResourceVisible shaderVisible = FillShaderVisible(i, binds, KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV);
                        m_ShaderNameToSlot.at(i).insert({ pAccelerationStructure->GetName(), shaderVisible });
                        SRVBaseIndexReflect.at(i) = std::min(shaderVisible.slot.bindingSlotIndex, SRVBaseIndexReflect.at(i));
                        SRVCountReflect.at(i) += 1;
                    }
                }
            }

            /// 统计所有的UAV
            for (auto it : GetAllBufRefl())
            {
                KGFX_ProgramUniformBlockDX12* pBlock = &it;

                if (pBlock->GetType() == SSBO_UNIFORM)
                {
                    uint32_t BindSlotCount = pBlock->GetBufArrSize();
                    for (int i = 0; i < pBlock->m_CBufBinds.size(); i++)
                    {
                        auto& binds = pBlock->m_CBufBinds.at(i);

                        if (binds.IsValid())
                        {
                            ShaderResourceVisible shaderVisible = FillShaderVisible(i, binds, KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV);
                            hasDescriptorStage.at(i) = true;
                            m_ShaderNameToSlot.at(i).insert({ pBlock->GetName(), shaderVisible });
                            UAVBaseIndexReflect.at(i) = std::min(shaderVisible.slot.bindingSlotIndex, UAVBaseIndexReflect.at(i));
                            UAVCountReflect.at(i) += BindSlotCount;
                        }
                    }
                }
            }
            std::for_each(UAVCountReflect.begin(), UAVCountReflect.end(), [this](uint32_t count) {
                m_UAVCount += count;
                });

            /// 统计所有的Sampler
            for (auto it : GetAllSamplerRefl())
            {
                KGFX_ProgramUniformSamplerDX12* pSampler = &it;

                for (int i = 0; i < pSampler->m_SamplerBinds.size(); i++)
                {
                    auto& binds = pSampler->m_SamplerBinds.at(i);

                    if (binds.IsValid())
                    {
                        hasDescriptorStage.at(i) = true;
                        ShaderResourceVisible shaderVisible = FillShaderVisible(i, binds, KGfxResourceViewType::RESOURCE_VIEW_TYPE_SAMPLER);
                        m_ShaderNameToSlot.at(i).insert({ pSampler->GetName(), shaderVisible });
                        SampleBaseIndexReflect.at(i) = std::min(shaderVisible.slot.bindingSlotIndex, SampleBaseIndexReflect.at(i));
                        SamplerCountReflect.at(i) += 1;
                    }
                }
            }
            std::for_each(SamplerCountReflect.begin(), SamplerCountReflect.end(), [this](uint32_t count) {
                m_SamplerCount += count;
                });

            m_ShaderRootConstBufferCount = static_cast<int>(vecParam.size());

            for (int i = 0; i < SRVCountReflect.size(); i++)
            {
                auto stageRefCount = SRVCountReflect.at(i);
                auto stageBaseIndex = SRVBaseIndexReflect.at(i);
                if (stageRefCount > 0)
                {
                    CD3DX12_DESCRIPTOR_RANGE SRVRange = {};
                    SRVRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, stageRefCount, stageBaseIndex, 0);
                    vecAllRangeTable.emplace_back(SRVRange);

                    CD3DX12_ROOT_PARAMETER SRVTable = {};
                    SRVTable.InitAsDescriptorTable(1, &vecAllRangeTable.back(), IndexToShaderVisib(i));
                    uRootSignatureSize += 1;
                    vecParam.emplace_back(SRVTable);
                }
            }
            m_ShaderStageSRVCount = SRVCountReflect;
            m_ShaderStageSRVBaseIndex = SRVBaseIndexReflect;

            for (int i = 0; i < UAVCountReflect.size(); i++)
            {
                auto stageRefCount = UAVCountReflect.at(i);
                auto stageBaseIndex = UAVBaseIndexReflect.at(i);
                if (stageRefCount > 0)
                {
                    CD3DX12_DESCRIPTOR_RANGE UAVRange = {};
                    UAVRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, stageRefCount, stageBaseIndex, 0);
                    vecAllRangeTable.emplace_back(UAVRange);

                    CD3DX12_ROOT_PARAMETER UAVTable = {};
                    UAVTable.InitAsDescriptorTable(1, &vecAllRangeTable.back(), IndexToShaderVisib(i));
                    uRootSignatureSize += 1;
                    vecParam.emplace_back(UAVTable);
                }
            }
            m_ShaderStageUAVCount = UAVCountReflect;
            m_ShaderStageUAVBaseIndex = UAVBaseIndexReflect;

            for (int i = 0; i < SamplerCountReflect.size(); i++)
            {
                auto stageRefCount = SamplerCountReflect.at(i);
                auto stageBaseIndex = SampleBaseIndexReflect.at(i);
                if (stageRefCount > 0)
                {
                    CD3DX12_DESCRIPTOR_RANGE SampleRange = {};
                    SampleRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, stageRefCount, stageBaseIndex, 0);
                    vecAllRangeTable.emplace_back(SampleRange);

                    CD3DX12_ROOT_PARAMETER SampleTable = {};
                    SampleTable.InitAsDescriptorTable(1, &vecAllRangeTable.back(), IndexToShaderVisib(i));
                    uRootSignatureSize += 1;
                    vecParam.emplace_back(SampleTable);
                }
            }
            m_ShaderStageSampleCount = SamplerCountReflect;
            m_ShaderStageSamplerIndex = SampleBaseIndexReflect;

            {
                assert(uRootSignatureSize <= 64);
                D3D12_ROOT_SIGNATURE_FLAGS rootSigFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
                for (uint32_t i = 0; i < hasDescriptorStage.size(); ++i)
                {
                    if (!hasDescriptorStage.at(i) && (i < hasDescriptorStage.size() - 1))
                    {
                        rootSigFlags |= static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(1 << (i + 1));
                    }
                }
                //rootSigFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
                CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(static_cast<uint32_t>(vecParam.size()), vecParam.data(), 0, nullptr, rootSigFlags);

                // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
                CComPtr<ID3DBlob> serializedRootSig = nullptr;
                CComPtr<ID3DBlob> errorBlob = nullptr;

                //D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
                //versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
                //versionedDesc.Desc_1_1 = rootSigDesc;

                //hResult = D3D12SerializeVersionedRootSignature(&versionedDesc, &serializedRootSig, &errorBlob);

                hResult = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);
                m_uRootSignatureHash = Fnv1a64(serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize());

                if (errorBlob)
                {
                    KGLogPrintf(KGLOG_ERR, "%s", static_cast<char*>(errorBlob->GetBufferPointer()));
                }

                KGLOG_COM_ASSERT_EXIT(hResult);

                CComPtr<ID3D12RootSignature> outRootSignature = nullptr;

                auto findRes = m_RootSignatureHashTable.find(m_uRootSignatureHash);

                if (findRes != m_RootSignatureHashTable.end())
                {
                    m_RootSignature = findRes->second;
                    m_RootSignature->AddRef();
                }
                else
                {
                    hResult = pD3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&outRootSignature));
                    KGLOG_COM_ASSERT_EXIT(hResult);
                    m_RootSignature = (outRootSignature.Detach());
                    m_RootSignatureHashTable.insert({ m_uRootSignatureHash, m_RootSignature });
#ifdef _DEBUG
                    size_t pos = m_szKey.find('@');
                    std::string beforeAt = (pos != std::string::npos) ? m_szKey.substr(0, pos) : m_szKey;
                    int len = MultiByteToWideChar(CP_UTF8, 0, beforeAt.c_str(), -1, nullptr, 0);
                    std::wstring wKey(len, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, beforeAt.c_str(), -1, wKey.data(), len);
                    m_RootSignature->SetName(wKey.c_str());
#endif
                }

            }

            {
                m_CPUBindToDescriptorTableOffset.resize(m_SRVCount + m_UAVCount);
                m_SamplerBindToDescriptorTableOffset.resize(m_SamplerCount);

                int index = 0;
                int indexSampler = 0;

                for (auto& nameSlot : m_ShaderNameToSlot)
                {
                    for (const auto& it : nameSlot)
                    {
                        if (it.second.slotType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SRV)
                        {
                            int startindex = GetSRVStageStartIndex(it.second.stageType);
                            int baseIndex = GetSRVStageBaseIndex(it.second.stageType);
                            startindex += it.second.slot.bindingSlotIndex - baseIndex;
                            m_CPUBindToDescriptorTableOffset.at(index) = startindex;

                            std::invoke(insertFun, it.first, index++);
                            //m_ShaderNameToCPUBind.insert({ it.first, index++ });

                        }
                        else if (it.second.slotType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_UAV)
                        {
                            int srvCount = GetSRVStageStartIndex(ShaderStageType::CountOf);
                            int startindex = GetUAVStageStartIndex(it.second.stageType);
                            int baseIndex = GetUAVStageBaseIndex(it.second.stageType);

                            startindex += it.second.slot.bindingSlotIndex + srvCount - baseIndex;
                            m_CPUBindToDescriptorTableOffset.at(index) = startindex;
                            std::invoke(insertFun, it.first, index++);
                            //m_ShaderNameToCPUBind.insert({ it.first, index++ });
                        }
                        else if (it.second.slotType == KGfxResourceViewType::RESOURCE_VIEW_TYPE_SAMPLER)
                        {
                            int startindex = GetSamplerStageStartIndex(it.second.stageType);
                            int baseIndex = GetSamplerStageBaseIndex(it.second.stageType);
                            startindex += (it.second.slot.bindingSlotIndex - baseIndex);

                            m_SamplerBindToDescriptorTableOffset.at(indexSampler) = startindex;
                            std::invoke(insertSamplerFun, it.first, indexSampler++);
                            //m_ShaderNameToCPUSampler.insert({ it.first, indexSampler++ });
                        }

                    }
                }
            }
        }
        return true;
    Exit0:
        return false;
    }

    const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageSRVCount() const
    {
        return m_ShaderStageSRVCount;
    }

    const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageUAVCount() const
    {
        return m_ShaderStageUAVCount;
    }

    const std::array<uint32_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageSampleCount() const
    {
        return m_ShaderStageSampleCount;
    }

    const std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageSRVBaseIndex() const
    {
        return m_ShaderStageSRVBaseIndex;
    }

    const std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageUAVBaseIndex() const
    {
        return m_ShaderStageUAVBaseIndex;
    }

    const std::array<uint16_t, CalculateGraphicsAndComputeShaderStageTypeCount()>& KGFX_ProgramReflectorDx12::GetShaderStageSamplerIndex() const
    {
        return m_ShaderStageSamplerIndex;
    }

    const KGFX_ProgramPushConstDX12& KGFX_ProgramReflectorDx12::GetPushConstRefl() const
    {
        return m_PushConst;
    }

    int KGFX_ProgramReflectorDx12::GetSRVStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < GetShaderStageSRVCount().size())
            {
                count += GetShaderStageSRVCount().at(index++);
            }
        }
        return count;

    }

    int KGFX_ProgramReflectorDx12::GetSRVStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = GetShaderStageSRVBaseIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }

    int KGFX_ProgramReflectorDx12::GetUAVStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < GetShaderStageUAVCount().size())
            {
                count += GetShaderStageUAVCount().at(index++);
            }
        }
        return count;
    }

    int KGFX_ProgramReflectorDx12::GetUAVStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = GetShaderStageUAVBaseIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }

    int KGFX_ProgramReflectorDx12::GetSamplerStageStartIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = 0;
        int index = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex); value < static_cast<uint32_t>(eStage); value <<= 1)
        {
            if (index < GetShaderStageSampleCount().size())
            {
                count += GetShaderStageSampleCount().at(index++);
            }
        }
        return count;
    }

    int KGFX_ProgramReflectorDx12::GetSamplerStageBaseIndex(ShaderStageType eStage) const
    {
        assert(eStage <= ShaderStageType::CountOf && eStage >= ShaderStageType::Vertex);
        int count = GetShaderStageSamplerIndex().at(CalculateShaderStageTypeIndex(eStage));
        return count;
    }

    void KGFX_CreateShaderPoolDx12()
    {
        if (!g_pShaderResourePoolDx12)
        {
            g_pShaderResourePoolDx12 = new KGFX_ShaderResourcePoolDx12;
        }
    }

    void KGFX_DestroyShaderPoolDx12()
    {
        SAFE_DELETE(g_pShaderResourePoolDx12);
    }

    KGFX_ShaderResourcePoolDx12* KGFX_GetShaderPoolDx12()
    {
        return g_pShaderResourePoolDx12;
    }
} // namespace gfx
#endif
