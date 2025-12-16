#pragma once
#include "KEnginePub/Public/IGFX_Public.h"
#include <d3d12.h>

namespace gfx
{
    class KGFX_ProgramReflectorDx12;
    class KGFX_ShaderDx12;
    class KGFX_ShaderReflectorDx12;


    class KGFX_PipelineDX12 : public KGfxRef, public KGFX_DelayReleaseObject
    {
    public:
        KGFX_PipelineDX12();
        ~KGFX_PipelineDX12() override;
        KGFX_PipelineDX12(const KGFX_PipelineDX12&) = delete;
        KGFX_PipelineDX12& operator=(const KGFX_PipelineDX12&) = delete;
        KGFX_PipelineDX12(KGFX_PipelineDX12&&) = delete;
        KGFX_PipelineDX12& operator=(KGFX_PipelineDX12&&) = delete;
        uint32_t GetCreateId();
        ID3D12PipelineState* GetPipelineHandle() const;

        bool CreateGraphicsPipeline(const KGfxFrameBufferDesc& pDesc, KRenderState& state, const std::vector<D3D12_INPUT_ELEMENT_DESC>& vecInputLayout, KGFX_ProgramReflectorDx12& pReflector);

        bool CreateComputePipeline(KGFX_ProgramReflectorDx12* pReflector);

        D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const;

        int32_t Release() override;

        const std::vector<D3D12_INPUT_ELEMENT_DESC>& GetInputLayout()const;

        uint32_t GetVertexBufStrid(uint32_t index) const;

        uint32_t GetStencileRef() const;
#ifdef _DEBUG
        std::string m_DeBugName = {};
#endif
    private:
        std::wstring BuildComputePsoHashName(const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc, KGFX_ProgramReflectorDx12* pReflector) ;
        std::wstring BuildGraphicsPsoHashName(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, KGFX_ProgramReflectorDx12& pReflector) ;

        bool CheckDepthFormat(gfx::enumTextureFormat dsv) const;
        ID3D12PipelineState* m_pipelineState = nullptr;
        D3D12_PRIMITIVE_TOPOLOGY m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        std::vector<D3D12_INPUT_ELEMENT_DESC> m_vecInputLayout = {};
        uint32_t m_StencileRef = 0;
        uint32_t m_uCreateId = 0;

    };
}
