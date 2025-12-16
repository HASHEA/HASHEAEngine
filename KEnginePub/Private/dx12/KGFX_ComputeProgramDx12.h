#pragma once
#ifdef _WIN32
#include "KGFX_DescriptorDX12.h"
#include "../IGFX_Private.h"
#include "../../Public/IGFX_Public.h"

namespace gfx
{
    class KGFX_PipelineDX12;
    class KGFX_ProgramBinderDx12;
    class KGFX_ShaderDx12;
    class KGFX_ShaderReflectorDx12;

    class KGFX_ComputeProgramDx12 final : public IKGFX_ComputeProgram
    {
    public:
        KGFX_ComputeProgramDx12();

        ~KGFX_ComputeProgramDx12() override;
        KGFX_ComputeProgramDx12(const KGFX_ComputeProgramDx12&) = delete;
        KGFX_ComputeProgramDx12& operator=(const KGFX_ComputeProgramDx12&) = delete;
        KGFX_ComputeProgramDx12(const KGFX_ComputeProgramDx12&&) = delete;
        KGFX_ComputeProgramDx12& operator=(const KGFX_ComputeProgramDx12&&) = delete;

        BOOL LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd, int nPlatform, KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD) override;
        BOOL Apply(IKGFX_RenderContext* pRenderCtx) override;
        uint32_t GetCurrentPipelineCode() override;
        BOOL SetConstDataBlock(IKGFX_RenderContext* pRenderCtx, uint32_t uSize, void* pData) override;
        IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) override;
        BOOL EndBind() override;
        void SetBeginBind(BOOL bBeginBind) override;
        BOOL LoadFromFile() override;
        BOOL IsLoaded() override;
        BOOL IsLoadFailed() override;
        BOOL IsLoading() override;
    private:
       
        bool PostLoad();

        bool PreparePipeline();

        bool BuildPipeline(KGFX_PipelineDX12** ppPipeline) const;

    public:
        BOOL IsReady() override;

    private:
        void Reset();

    public:
        IKGFX_ProgramBinder* GetProgramBinder() override;
        IKGFX_ProgramBinder& BeginBind() override;

    private:
        IKGFX_Program::KGFX_ProgramLoadParam m_LoadParam = {};

        /**
		 * 用来记录绑定的参数
		 */
        KGFX_ProgramBinderDx12* m_pProgramBinder = nullptr;

        bool m_bReady = false;

        KGFX_PipelineDX12* m_pPipeline = nullptr;

        std::atomic<BOOL> m_bLoaded = {};        
    };
};

#endif
