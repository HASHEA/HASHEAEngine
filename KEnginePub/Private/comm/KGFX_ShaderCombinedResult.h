#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include "KBase/Public/KBasePub.h"
#include "KEnginePub/Private/IKShaderreflector.h"

namespace gfx
{
    /////////////////////////////////////////////////////////////////////////////////////////

    class KGFX_CombinedShaderResult : public IKGFX_CombinedShaderResult
    {
    public:
        const char* GetShaderResult(gfx::ShaderStageType eShaderStage) override;
        uint32_t    GetShaderResultSize(gfx::ShaderStageType eShaderStage) override;
        void        SetPreviousPushConstsSize(uint32_t uSize) override;
        uint32_t    GetPreviousPushConstsSize() override;
        void        Clear() override;

    protected:
        std::string  m_szBinedShaderString[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];
        std::string* _GetResultString(gfx::ShaderStageType eShaderStage);
        uint32_t     m_uPreviousConstsSize = 0;
    };

    /////////////////////////////////////////////////////////////////////////////////////////

    class KGFX_CombinedShaderResultVK_HLSL : public KGFX_CombinedShaderResult
    {
    public:
        KGFX_CombinedShaderResultVK_HLSL();
        ~KGFX_CombinedShaderResultVK_HLSL();
        BOOL             Fix(const char* pcszShaderName, const char* pcszSrcShaderContent, gfx::ShaderStageType eShaderStage) override;
        virtual uint32_t GetSamplerCount(gfx::ShaderStageType eShaderStage) override;
        virtual BOOL     GetSamplerState(gfx::ShaderStageType eShaderStage, uint32_t i, gfx::KSamplerState** ppOutSamplerState, char* pOutName, uint32_t pOutNameSize) override;

    private:
        std::map<std::string, uint32_t> m_mapVertexOutLocationTable;
        std::map<std::string, uint32_t> m_mapBindingTable;
        uint32_t                        m_uMaxBindingId = 0;

        std::map<std::string, uint32_t> m_mapSpecializationTable;
        uint32_t                        m_uMaxSpecializationId = 0;

        struct _SamplerStateItem
        {
            std::string   name;
            KSamplerState samplerState;

            _SamplerStateItem()
            {
                samplerState.bNeedShaderInit = true;
            }
        };

        std::vector<_SamplerStateItem*> m_vecSamplerState[SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT];
        BOOL                            m_bWholeOnePushConst;
    };


    /////////////////////////////////////////////////////////////////////////////////////////
    class KGFX_CombinedShaderResultDx12 : public KGFX_CombinedShaderResult
    {
    public:
        KGFX_CombinedShaderResultDx12();
        ~KGFX_CombinedShaderResultDx12();
        BOOL             Fix(const char* pcszShaderName, const char* pcszSrcShaderContent, gfx::ShaderStageType eShaderStage) override;
        virtual uint32_t GetSamplerCount(gfx::ShaderStageType eShaderStage) override;
        virtual BOOL     GetSamplerState(gfx::ShaderStageType eShaderStage, uint32_t i, gfx::KSamplerState** ppOutSamplerState, char* pOutName, uint32_t pOutNameSize) override;

    private:
    };

}; // namespace gfx
