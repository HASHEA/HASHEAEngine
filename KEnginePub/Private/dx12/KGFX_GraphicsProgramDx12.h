#pragma once
#ifdef _WIN32
#include <filesystem>
#include "KGFX_DescriptorDX12.h"
#include "KGFX_RefPtr.h"
#include "../IGFX_Private.h"
#include "../../Public/IGFX_Public.h"
#include "KBase/Public/io/KFile.h"

namespace gfx
{
    class KGFX_PipelineDX12;
    class KGFX_ProgramBinderDx12;
    class KGFX_ShaderDx12;
    class KGFX_ShaderReflectorDx12;

    class KGFX_GraphicsProgramDx12 : public IKGFX_GraphicsProgram
    {
    public:
        KGFX_GraphicsProgramDx12();

        ~KGFX_GraphicsProgramDx12() override;
        KGFX_GraphicsProgramDx12(const KGFX_GraphicsProgramDx12&)             = delete;
        KGFX_GraphicsProgramDx12& operator=(const KGFX_GraphicsProgramDx12&)  = delete;
        KGFX_GraphicsProgramDx12(const KGFX_GraphicsProgramDx12&&)            = delete;
        KGFX_GraphicsProgramDx12& operator=(const KGFX_GraphicsProgramDx12&&) = delete;

        BOOL LoadGraphicsShader(
            const char*                     szShaderSource,
            const NSKBase::tagFileLocation& sIncludeShaderLoc,
            const char*                     szShaderDef,
            const char*                     szMacro,
            BOOL                            bReCreate,
            BOOL                            bByBuildToolCmd = false,
            int                             nPlatform       = 0,
            KEnumMtlTaskLevel               uThreadLevel    = KEnumMtlTaskLevel::DISABLE_MTL_THREAD
        ) override;

        BOOL LoadFromFile() override;
        BOOL PostLoad() override;
        BOOL IsLoaded() override;
        BOOL IsLoadFailed() override;
        BOOL IsLoading() override;

        void ClearVertDesc() override;


        IKGFX_GraphicsProgram& BeginVertDesc() override;


        IKGFX_GraphicsProgram& AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate) override;


        IKGFX_GraphicsProgram& AddAttribute(gfx::KVertexDecl* pDecl, uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset) override;


        BOOL EndVertDesc() override;

        BOOL BindVertAttr(gfx::KVertexDecl* pDecls[], uint32_t uDeclCount) override;


        BOOL ApplyRenderState(const std::function<void(gfx::KRenderState*)>& fnRenderStateDefineCall, gfx::KRenderState* pRenderState = nullptr) override;


        BOOL IsReady() override;

        // 废弃
        //BOOL SetVSConstDataBlock(uint32_t uSize, void* pData) override;
        //BOOL SetFSConstDataBlock(uint32_t uSize, void* pData) override;
        //BOOL SetConstDataBlock(gfx::ShaderStageType shaderType, uint32_t uSize, void* pData) override;

        // 统一成这个
        BOOL SetConstDataBlock(uint32_t uSize, void* pData) override;

        int AddRef() override;


        int GetRef() override;


        int Release() override;

        BOOL SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize) override;

        BOOL PreparePipeline() override;
        BOOL ApplyPipeline() override;
        uint32_t GetCurrentPipelineCode() override;
        BOOL UpdateMtlData() override;

        IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;

        BOOL                     EndBind() override;
        void                     SwapBindData(IKGFX_GraphicsProgram* pProgram) override;
        BOOL                     IsTextureBinded(const_pool_str pName) override;
        const gfx::KRenderState& GetSrcRenderState() override;
        gfx::KRenderState*       GetRenderState() override;
        BOOL                     IsBeginBind() override;
        void                     SetBeginBind(BOOL bBeginBind) override;
        BOOL                     IsActiveBlock(const_pool_str pcszName) override;
        void GetUserShaderDetail(int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask) override;
    private:
        BOOL BuildPipeline(IKGFX_RenderContext* pRenderCtx, gfx::KRenderState& state, KGFX_PipelineDX12** ppPipeline) const;

    public:
        IKGFX_ProgramBinder* GetProgramBinder() override;
        void                 AddSamplerState(const char* pName, gfx::KSamplerState& samplerState) override;

    private:
        /**
         * 用来记录绑定的参数
         */
        RefPtr<KGFX_ProgramBinderDx12> m_pProgramBinder = nullptr;

#pragma region pipelineStateAndInput
        struct InputBindDesc
        {
            uint32_t            m_binding;
            uint32_t            m_stride;
            enumVertexInputRate m_inputRate;
        };

        struct InputeAttribute
        {
            gfx::KVertexDecl* m_pDecl;
            uint32_t          m_binding;
            uint32_t          m_location;
            enumVertexFormat  m_format;
            uint32_t          m_offset;
        };

        std::vector<InputBindDesc>            m_vecInputBindDesc   = {};
        std::vector<InputeAttribute>          m_vecInputeAttribute = {};
        std::vector<D3D12_INPUT_ELEMENT_DESC> m_vecInputLayout     = {};

        gfx::KRenderState m_srcRenderState = {};
        gfx::KRenderState m_dstRenderState = {};
#pragma endregion

        bool                                             m_bReady     = false;
        std::unordered_map<uint32_t, KAutoRefPtr<KGFX_PipelineDX12>> m_mapPipline = {};
        KGFX_PipelineDX12* m_pPipeline  = nullptr;

        const gfx::RenderCommonParam* m_pCurrentCommonParam = nullptr;

        gfx::IKGFX_Program::KGFX_ProgramLoadParam m_LoadParam = {};
        std::atomic<BOOL>                                 m_bLoaded   = {};
    };

}; // namespace gfx

#endif
