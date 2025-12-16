#pragma once
#include <stdint.h>
#include <string>
#include "KEnginePub/Public/IGFX_Public.h"
#include "KEnginePub/Public/IKLoadable.h"

typedef int BOOL;

class KByteBufferStream;

namespace gfx
{

	typedef uint32_t ShaderStageFlagsBit;

	static const char* g_strProgramDataTypeName[] =
	{
		"FLOAT1_TYPE",
		"FLOAT2_TYPE",
		"FLOAT3_TYPE",
		"FLOAT4_TYPE",
		"FLOAT4X4_TYPE",

		"FLOAT1_ARRAY_TYPE",
		"FLOAT2_ARRAY_TYPE",
		"FLOAT3_ARRAY_TYPE",
		"FLOAT4_ARRAY_TYPE",
		"FLOAT4X4_ARRAY_TYPE",

		"INT1_TYPE",
		"INT2_TYPE",
		"INT3_TYPE",
		"INT4_TYPE",
		"INT_ARRAY_TYPE",

		"UINT1_TYPE",
		"UINT2_TYPE",
		"UINT3_TYPE",
		"UINT4_TYPE",
		"UINT_ARRAY_TYPE",

		"IMAGE_TEXTURE",
		"IMAGE_TEXTURES",
		"USER_TYPE",
	};

	static_assert(
		sizeof(g_strProgramDataTypeName) / sizeof(g_strProgramDataTypeName[0]) == (uint32_t)enumProgramDataType::DATA_TYPE_COUNT,
		"enumProgramDataType g_strProgramDataTypeName must have same size"
		);

	inline const char* GetDataTypeName(enumProgramDataType type)
	{
		return g_strProgramDataTypeName[(int)type];
	}

	
    class IKGFX_CombinedShaderResult
    {
    public:
        virtual ~IKGFX_CombinedShaderResult() {}
        virtual BOOL Fix(const char* pcszShaderName, const char* szSrcShaderContent, gfx::ShaderStageType eShaderStage) = 0;
        virtual const char* GetShaderResult(gfx::ShaderStageType eShaderStage) = 0;
        virtual uint32_t GetShaderResultSize(gfx::ShaderStageType eShaderStage) = 0;
        virtual void SetPreviousPushConstsSize(uint32_t uSize) = 0;
        virtual uint32_t GetPreviousPushConstsSize() = 0;
        virtual uint32_t GetSamplerCount(gfx::ShaderStageType eShaderStage) = 0;
        virtual BOOL GetSamplerState(gfx::ShaderStageType eShaderStage, uint32_t i, gfx::KSamplerState** ppOutSamplerState, char* pOutName, uint32_t pOutNameSize) = 0;
        virtual void Clear() = 0;
    };

    struct IShaderReflectorBase :public IKShaderResource
    {
        //virtual BOOL BuildReflection(void* pProgram, gfx::ShaderStageType shaderType) = 0;
        virtual BOOL BuildReflectionSpirvCross(void* pProgramCross, gfx::ShaderStageType shaderType) = 0;
        virtual IKGFX_CombinedShaderResult* GetCombindShaderResult() = 0;
    };

	struct IShaderReflector : public IShaderReflectorBase
	{
        virtual BOOL IsReflected(ShaderStageType shaderType) {return false;};
		
        virtual uint32_t GetPushContentAlign16BytesBlockSize() {return 0;};

        virtual uint32_t GetVsPushConstantSize() {return 0;};
        virtual void SetVsPushContantSize(uint32_t uVsPushContantSize) {};

        virtual int32_t GetMaxBinding() {return 0;};

        virtual void SetShaderFileName(const char* pName, ShaderStageType shaderType) {};
        virtual const char* GetShaderFileName(ShaderStageType shaderType) {return nullptr;};

		virtual BOOL FindBindingForFixShaderContent(const char* szBlockName, int32_t& binding) { return false; };
		virtual void InsertBindingForFixShaderContent(const char* szBlockName, int32_t binding) {};

		virtual BOOL HasVsShaderTmpData() { return false; }
		virtual void  SetupVsShaderTmpData(const char* iopath, const char* szShaderName, const char* pString, uint32_t stage, uint32_t shadertype, uint32_t shaderTypeBit) {};
		virtual void SetupVsShaderTmpData(uint32_t* pVsSpirv, uint32_t uIntCount) {};
		virtual uint32_t GetVsShaderTmpSpirvUintCount() {return 0;}
		virtual uint32_t *GetVsTmpSpirv(){return nullptr;};
		virtual const char* GetVsTmp_iopath() { return nullptr; }
		virtual const char* GetVsTmp_szShaderName() { return nullptr; }
		virtual const char* GetVsTmp_vsString() { return nullptr; }
		virtual uint32_t GetVsTmp_stage() { return 0; }
		virtual uint32_t GetVsTmp_shaderType() { return 0; }
		virtual uint32_t GetVsTmp_shaderTypeBit() { return 0; }
		virtual void ClearVsTmpData() {}
        virtual BOOL IsLogShader() {return false;};
        virtual void AddSpecializationConstDefine(uint32_t uStageType, uint32_t const_id, const char* pName) {};
		virtual void DebugPrintSaveReflectorBegin(const char* pFileName) {};
		virtual void DebugPrintSaveReflectorEnd(const char* pFileName) {};
		virtual BOOL IsEnableSpirvCrossReflector() { return false;};

		virtual void SetIsHLSL(BOOL bHLSL)
		{
			m_bHLSL = bHLSL;
		}

		virtual BOOL IsHLSL()
		{
			return m_bHLSL;
		}
	public:
		void SetPlatform(int nPlatform) { m_nPlatform = nPlatform; }
		int  GetPlatform() { return m_nPlatform; }

	public:
		BOOL bByBuildTool = false;
		BOOL bForMaterialSystem = true;
		BOOL bVsChanged = false;
		
	private:
		int m_nPlatform = 0;
		BOOL m_bHLSL = false;
	};
};
