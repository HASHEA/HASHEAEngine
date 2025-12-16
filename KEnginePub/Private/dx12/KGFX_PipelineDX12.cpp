#include "KGFX_PipelineDX12.h"
#include "KGFX_GraphiceDeviceDX12.h"
#include "KGFX_HashFunDX12.h"
#include "KGFX_PipelineCacheDX12.h"
#include "KGFX_ShaderResourceDx12.h"


namespace gfx
{

    KGFX_PipelineDX12::KGFX_PipelineDX12()
    {
        static std::atomic<uint32_t> ctorid{0};
        m_uCreateId = ++ctorid;
    }

    KGFX_PipelineDX12::~KGFX_PipelineDX12()
    {
        SAFE_RELEASE(m_pipelineState);
    }

    uint32_t KGFX_PipelineDX12::GetCreateId()
    {
        return m_uCreateId;
    }

    ID3D12PipelineState* KGFX_PipelineDX12::GetPipelineHandle() const
    {
        return m_pipelineState;
    }

    bool KGFX_PipelineDX12::CreateGraphicsPipeline(const KGfxFrameBufferDesc& pDesc, KRenderState& state,
        const std::vector<D3D12_INPUT_ELEMENT_DESC>& vecInputLayout, KGFX_ProgramReflectorDx12& pReflector)
    {
        BOOL bResult = false;
        HRESULT hResult = E_FAIL;
        ASSERT(pDesc.vecFramebufferRTVDesc.size() <= MAX_RENDERTARGET_COUNT);
        ASSERT(pReflector.GetRootSignature());
        assert(m_pipelineState == nullptr);

        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();
        m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dx12GPipelineStateDesc = {};
        ID3D12PipelineState* pPSO = nullptr;

        dx12GPipelineStateDesc.pRootSignature = pReflector.GetRootSignature();

        /// shader code赋值
        for (int i = 0; i < pReflector.GetShaderCode().size(); i++)
        {
            const KGFX_ShaderDx12* eachShader = pReflector.GetShaderCode().at(i);
            switch (eachShader->GetShaderStage())
            {
            case ShaderStageType::Vertex:
                dx12GPipelineStateDesc.VS.pShaderBytecode = eachShader->GetCompliledShader()->GetBufferPointer();
                dx12GPipelineStateDesc.VS.BytecodeLength = eachShader->GetCompliledShader()->GetBufferSize();
                break;
            case ShaderStageType::Domain:
                dx12GPipelineStateDesc.DS.pShaderBytecode = eachShader->GetCompliledShader()->GetBufferPointer();
                dx12GPipelineStateDesc.DS.BytecodeLength = eachShader->GetCompliledShader()->GetBufferSize();
                break;
            case ShaderStageType::Hull:
                dx12GPipelineStateDesc.HS.pShaderBytecode = eachShader->GetCompliledShader()->GetBufferPointer();
                dx12GPipelineStateDesc.HS.BytecodeLength = eachShader->GetCompliledShader()->GetBufferSize();
                break;
            case ShaderStageType::Geometry:
                dx12GPipelineStateDesc.GS.pShaderBytecode = eachShader->GetCompliledShader()->GetBufferPointer();
                dx12GPipelineStateDesc.GS.BytecodeLength = eachShader->GetCompliledShader()->GetBufferSize();
                break;
            case ShaderStageType::Fragment:
                dx12GPipelineStateDesc.PS.pShaderBytecode = eachShader->GetCompliledShader()->GetBufferPointer();
                dx12GPipelineStateDesc.PS.BytecodeLength = eachShader->GetCompliledShader()->GetBufferSize();
                break;
            default:
                ;
            }
        }

        //IA
        {
            m_vecInputLayout = vecInputLayout;
            dx12GPipelineStateDesc.InputLayout = { vecInputLayout.data(), static_cast<uint32_t>(vecInputLayout.size()) };
            enumDrawMode eDrawMode = state.drawMode;
            switch (eDrawMode)
            {
            case PT_POINT_LIST:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
            case PT_LINE_LIST:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                break;
            case PT_LINE_STRIP:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                break;
            case PT_TRIANGLE_LIST:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
            case PT_TRIANGLE_STRIP:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                break;
            case PT_TRIANGLE_FAN:
                assert(false);
                KGLogPrintf(KGLOG_ERR, "%s", "dx12 不支持 TRIANGLE_FAN 绘制模式");
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
            case PT_PATCH:
                dx12GPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
                m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST; //to D3D_PRIMITIVE_TOPOLOGY_32_CONTROL_POINT_PATCHLIST			
                break;
            default:
                break;
            }
        }

        //RS
        {
            enumPolygonMode ePolygonMode = state.polygonMode;
            enumCullMode eCullMode = state.cullMode;
            enumFrontFaceMode eFrontFaceMode = state.frontFaceMode;

            dx12GPipelineStateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            dx12GPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = state.depthBiasSlopeFactor;
            dx12GPipelineStateDesc.RasterizerState.DepthBias = static_cast<uint32_t>(state.depthBiasConstantFactor);
            dx12GPipelineStateDesc.RasterizerState.DepthBiasClamp = state.depthBiasClamp;
            dx12GPipelineStateDesc.RasterizerState.DepthClipEnable = state.depthClampEnable;
            dx12GPipelineStateDesc.RasterizerState.AntialiasedLineEnable = true; //是不是效果会好点？

            switch (ePolygonMode)
            {
            case POLYGON_MODE_FILL:
                dx12GPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                break;
            case POLYGON_MODE_LINE:
                dx12GPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
                break;
            case POLYGON_MODE_POINT:
                //dx12没有point fill mode
                dx12GPipelineStateDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                break;
            default:
                break;
            }


            switch (eCullMode)
            {
            case CULL_MODE_NONE:
                dx12GPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                break;
            case CULL_MODE_FRONT:
                dx12GPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
                break;
            case CULL_MODE_BACK:
                dx12GPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
                break;
            case CULL_MODE_FRONT_AND_BACK:
                KGLogPrintf(KGLOG_ERR, "%s", "dx12 不支持 CULL_MODE_FRONT_AND_BACK 绘制模式");
                dx12GPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
                break;
            default:
                break;
            }

            switch (eFrontFaceMode)
            {
            case FRONT_FACE_COUNTER_CLOCKWISE:
                dx12GPipelineStateDesc.RasterizerState.FrontCounterClockwise = true;
                break;
            case FRONT_FACE_CLOCKWISE:
                dx12GPipelineStateDesc.RasterizerState.FrontCounterClockwise = false;
                break;
            default:
                break;
            }
        }

        // color blend
        {
            dx12GPipelineStateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            dx12GPipelineStateDesc.BlendState.AlphaToCoverageEnable = false;
            dx12GPipelineStateDesc.BlendState.IndependentBlendEnable = true;

            for (uint32_t i = 0; i < KMAX_BLEND_ATTACHMENT && i < state.blendAttachCount; ++i)
            {
                auto& att = state.blendAttachment[i];
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].BlendEnable = att.blendEnable;
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_CLEAR;
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].LogicOpEnable = false;
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = 0;
                if (att.writeR)
                {
                    dx12GPipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_RED;
                }
                if (att.writeG)
                {
                    dx12GPipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
                }
                if (att.writeB)
                {
                    dx12GPipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
                }
                if (att.writeA)
                {
                    dx12GPipelineStateDesc.BlendState.RenderTarget[i].RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
                }
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].SrcBlend = GetDxBlendFactor(att.srcColorBlendFactor);
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].DestBlend = GetDxBlendFactor(att.dstColorBlendFactor);
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].SrcBlendAlpha = GetDxBlendFactor(att.srcAlphaBlendFactor);
                dx12GPipelineStateDesc.BlendState.RenderTarget[i].DestBlendAlpha = GetDxBlendFactor(att.dstAlphaBlendFactor);
            }
        }
        // depthstencil
        {
            dx12GPipelineStateDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
            dx12GPipelineStateDesc.DepthStencilState.DepthEnable = state.depthTestEnable;
            dx12GPipelineStateDesc.DepthStencilState.DepthWriteMask = state.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            dx12GPipelineStateDesc.DepthStencilState.StencilReadMask = static_cast<uint8_t>(state.stencilFront.compareMask) | static_cast<uint8_t>(state.stencilBack.compareMask);
            dx12GPipelineStateDesc.DepthStencilState.StencilWriteMask = static_cast<uint8_t>(state.stencilFront.writeMask) | static_cast<uint8_t>(state.stencilBack.writeMask);
            dx12GPipelineStateDesc.DepthStencilState.StencilEnable = state.stencilTestEnable;
            dx12GPipelineStateDesc.DepthStencilState.DepthFunc = GetDxDepthCompareOp(state.depthCompareOp);

            dx12GPipelineStateDesc.DepthStencilState.FrontFace.StencilFunc = GetDxStencilCompareOp(state.stencilFront.stencilCompareOp);
            dx12GPipelineStateDesc.DepthStencilState.FrontFace.StencilDepthFailOp = GetDxStencilOp(state.stencilFront.stencilDepthFailOp);
            dx12GPipelineStateDesc.DepthStencilState.FrontFace.StencilPassOp = GetDxStencilOp(state.stencilFront.stencilPassOp);
            dx12GPipelineStateDesc.DepthStencilState.FrontFace.StencilFailOp      = GetDxStencilOp(state.stencilFront.sencilFailOp);
            
            dx12GPipelineStateDesc.DepthStencilState.BackFace.StencilFunc = GetDxStencilCompareOp(state.stencilBack.stencilCompareOp);
            dx12GPipelineStateDesc.DepthStencilState.BackFace.StencilDepthFailOp = GetDxStencilOp(state.stencilBack.stencilDepthFailOp);
            dx12GPipelineStateDesc.DepthStencilState.BackFace.StencilPassOp = GetDxStencilOp(state.stencilBack.stencilPassOp);
            dx12GPipelineStateDesc.DepthStencilState.BackFace.StencilFailOp      = GetDxStencilOp(state.stencilBack.sencilFailOp);

            m_StencileRef = state.stencilFront.reference | state.stencilBack.reference;
        }

        //rt 
        {
            dx12GPipelineStateDesc.NumRenderTargets = static_cast<uint32_t>(pDesc.vecFramebufferRTVDesc.size());
            for (uint32_t i = 0; i < pDesc.vecFramebufferRTVDesc.size(); ++i)
            {
                dx12GPipelineStateDesc.RTVFormats[i] = GetTexToDxFormat(pDesc.vecFramebufferRTVDesc.at(i).pTargetView->GetViewDesc().eFormat);
            }

            if (pDesc.DSVDesc.pTargetView)
            {
                dx12GPipelineStateDesc.DSVFormat = GetTexToDxFormat(pDesc.DSVDesc.pTargetView->GetViewDesc().eFormat);
            }

        }


        //multisamplestate
        {
            dx12GPipelineStateDesc.SampleDesc.Count = DrvOption::bEnableMSAA ? DrvOption::uMSAAQulity : 1;
            dx12GPipelineStateDesc.SampleDesc.Quality = DrvOption::bEnableMSAA ? DrvOption::uMSAAQulity - 1 : 0;
        }

        dx12GPipelineStateDesc.SampleMask = UINT_MAX;
        dx12GPipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS;
        auto pipelineLibrary = pGraphicDevice->GetPipelineCache()->GetPipelineLibrary();

        std::wstring hashWStr = BuildGraphicsPsoHashName(dx12GPipelineStateDesc, pReflector);
    
        hResult = pipelineLibrary->LoadGraphicsPipeline(hashWStr.c_str(), &dx12GPipelineStateDesc, IID_PPV_ARGS(&pPSO));
        if (!SUCCEEDED(hResult))
        {
            hResult = pD3dDevice->CreateGraphicsPipelineState(&dx12GPipelineStateDesc, IID_PPV_ARGS(&pPSO));
            assert(SUCCEEDED(hResult));
            pPSO->SetName(hashWStr.c_str());
            hResult = pipelineLibrary->StorePipeline(hashWStr.c_str(), pPSO);
            assert(SUCCEEDED(hResult));
        }
        else
        {
            int i = 0;
        }
      
        KGLOG_COM_ASSERT_EXIT(hResult);
        m_pipelineState = pPSO;

        return true;
    Exit0:
        return false;
    }


    bool KGFX_PipelineDX12::CreateComputePipeline(KGFX_ProgramReflectorDx12* pReflector)
    {
        assert(m_pipelineState == nullptr);
        KGFX_GraphicDeviceDx12* pGraphicDevice = dynamic_cast<KGFX_GraphicDeviceDx12*>(KGFX_GetGraphicDevice());
        ID3D12Device* pD3dDevice = pGraphicDevice->GetDXDevice();

        D3D12_COMPUTE_PIPELINE_STATE_DESC dx12GPipelineStateDesc = {};
        ID3D12PipelineState* pPSO = nullptr;

        dx12GPipelineStateDesc.pRootSignature = pReflector->GetRootSignature();
        assert(pReflector->GetShaderCode().at(0));
        assert(pReflector->GetShaderCode().at(0)->GetShaderStage() == ShaderStageType::Compute);
        dx12GPipelineStateDesc.CS.pShaderBytecode = pReflector->GetShaderCode().at(0)->GetCompliledShader()->GetBufferPointer();
        dx12GPipelineStateDesc.CS.BytecodeLength = pReflector->GetShaderCode().at(0)->GetCompliledShader()->GetBufferSize();

        dx12GPipelineStateDesc.NodeMask = 0;
        dx12GPipelineStateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        auto pipelineLibrary = pGraphicDevice->GetPipelineCache()->GetPipelineLibrary();

        std::wstring hashWStr = BuildComputePsoHashName(dx12GPipelineStateDesc, pReflector);
        HRESULT hResult = pipelineLibrary->LoadComputePipeline(hashWStr.c_str(), &dx12GPipelineStateDesc, IID_PPV_ARGS(&pPSO));
  
        if (!SUCCEEDED(hResult))
        {
            hResult = pD3dDevice->CreateComputePipelineState(&dx12GPipelineStateDesc, IID_PPV_ARGS(&pPSO));
            assert(SUCCEEDED(hResult));
            hResult = pipelineLibrary->StorePipeline(hashWStr.c_str(), pPSO);
            assert(SUCCEEDED(hResult));
        }
       
        KGLOG_COM_ASSERT_EXIT(hResult);
        m_pipelineState = pPSO;

        return true;
    Exit0:
        return false;
    }

    D3D12_PRIMITIVE_TOPOLOGY KGFX_PipelineDX12::GetPrimitiveTopology() const
    {
        return m_primitiveTopology;
        //return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        //return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        //return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        //return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        //return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        //return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
    }

    int32_t KGFX_PipelineDX12::Release()
    {
        int nRef = --m_nRef;
        ASSERT(nRef >= 0);
        if (nRef == 0)
        {
            if (m_pipelineState)
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

    const std::vector<D3D12_INPUT_ELEMENT_DESC>& KGFX_PipelineDX12::GetInputLayout()const
    {
        return  m_vecInputLayout;
    }

    uint32_t KGFX_PipelineDX12::GetVertexBufStrid(uint32_t index) const
    {
        uint32_t strid = 0;
        for (auto& layout : m_vecInputLayout)
        {
            TextureFormatInfo formatInfo = GetDX12FormatInfo(layout.Format);
            if (layout.InputSlot==index)
            {
                strid += formatInfo.uBytesPerBlock;
            }
        }
        return strid;
    }

    uint32_t KGFX_PipelineDX12::GetStencileRef() const
    {
        return m_StencileRef;
    }

    std::wstring KGFX_PipelineDX12::BuildComputePsoHashName(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, KGFX_ProgramReflectorDx12* pReflector) 
    {
        /// shadercode的hash
        uint64_t h = pReflector->GetShaderCode().at(0)->GetHashCode();

        /// rootsignature的hash
        h = Fnv1a64(pReflector->GetKey().data(), pReflector->GetKey().length(), h);

        /// NodeMask & Flags
        HashCombine(h, Fnv1a64(&desc.NodeMask, sizeof(desc.NodeMask)));
        HashCombine(h, Fnv1a64(&desc.Flags, sizeof(desc.Flags)));

        std::ostringstream oss;
        oss << "CP_" << std::hex << std::setw(16) << std::setfill('0') << h;
        std::string hashStr = oss.str();
#ifdef _DEBUG
        m_DeBugName = hashStr;
#endif
        std::wstring hashWStr = ToWide(hashStr);
        return hashWStr;
    }

    std::wstring KGFX_PipelineDX12::BuildGraphicsPsoHashName(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, KGFX_ProgramReflectorDx12& pReflector) 
    {
        uint64_t h = 0;
        /// shader code赋值
        for (uint32_t i = 0; i < pReflector.GetShaderCode().size(); i++)
        {
            const KGFX_ShaderDx12* eachShader = pReflector.GetShaderCode().at(i);
            HashCombine(h, eachShader->GetHashCode());
        }

        /// rootsignature的hash
        HashCombine(h, pReflector.GetRootSignatureHash());

        /// NodeMask & Flags
        HashCombine(h, Fnv1a64(&desc.NodeMask, sizeof(desc.NodeMask)));
        HashCombine(h, Fnv1a64(&desc.Flags, sizeof(desc.Flags)));
        HashCombine(h, Fnv1a64(&desc.BlendState, sizeof(desc.BlendState)));
        HashCombine(h, Fnv1a64(&desc.StreamOutput, sizeof(desc.StreamOutput)));
        HashCombine(h, Fnv1a64(&desc.RasterizerState, sizeof(desc.RasterizerState)));
        HashCombine(h, Fnv1a64(&desc.DepthStencilState, sizeof(desc.DepthStencilState)));
        // 新：稳定序列化 InputLayout
        {
            const auto* elems = desc.InputLayout.pInputElementDescs;
            uint32_t num = desc.InputLayout.NumElements;
            HashCombine(h, Fnv1a64(&num, sizeof(num)));
            for (uint32_t i = 0; i < num; ++i)
            {
                const auto& e = elems[i];
                if (e.SemanticName)
                {
                    HashCombine(h, Fnv1a64(e.SemanticName, strlen(e.SemanticName))); // 字符串内容
                }
                else
                {
                    uint8_t zero = 0;
                    HashCombine(h, Fnv1a64(&zero, 1));
                }
                HashCombine(h, Fnv1a64(&e.SemanticIndex, sizeof(e.SemanticIndex)));
                HashCombine(h, Fnv1a64(&e.Format, sizeof(e.Format)));
                HashCombine(h, Fnv1a64(&e.InputSlot, sizeof(e.InputSlot)));
                HashCombine(h, Fnv1a64(&e.AlignedByteOffset, sizeof(e.AlignedByteOffset)));
                HashCombine(h, Fnv1a64(&e.InputSlotClass, sizeof(e.InputSlotClass)));
                HashCombine(h, Fnv1a64(&e.InstanceDataStepRate, sizeof(e.InstanceDataStepRate)));
            }
        }
        HashCombine(h, Fnv1a64(&desc.IBStripCutValue, sizeof(desc.IBStripCutValue)));
        //HashCombine(h, Fnv1a64(&desc.PrimitiveTopologyType, sizeof(desc.PrimitiveTopologyType)));
        HashCombine(h, Fnv1a64(&desc.NumRenderTargets, sizeof(desc.NumRenderTargets)));
        for (uint32_t i = 0; i < desc.NumRenderTargets; ++i)
        {
            HashCombine(h, Fnv1a64(&desc.RTVFormats[i], sizeof(DXGI_FORMAT)));
        }
        HashCombine(h, Fnv1a64(&desc.DSVFormat, sizeof(DXGI_FORMAT)));
        HashCombine(h, Fnv1a64(&desc.SampleDesc, sizeof(desc.SampleDesc)));

        std::ostringstream oss;
        oss << "CP_" << std::hex << std::setw(16) << std::setfill('0') << h;
        std::string hashStr = oss.str();
#ifdef _DEBUG
        m_DeBugName = hashStr;
#endif

        std::wstring hashWStr = ToWide(hashStr);
        return hashWStr;
    }

    bool KGFX_PipelineDX12::CheckDepthFormat(enumTextureFormat dsv) const
    {
        switch (dsv)
        {
        case TEX_FORMAT_D24_UNORM_S8_UINT:
        case TEX_FORMAT_D16_UNORM:
        case TEX_FORMAT_D32_SFLOAT:
        case TEX_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

}
