#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include "dxc/dxcapi.h"
#include "dxc/dxctools.h"
#include "Base/hfile.h"
#include "Graphics/RHICommon.h"
#include "DXCIncludeHandler.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace RHI
{
	struct ShaderItem
	{
		const char* sourceShaderPath = nullptr;
		const char* userShaderPath = nullptr;
		const char* generatedBindingsPath = nullptr;
		const char* macroDefine = nullptr;
		const char* entryPoint = nullptr;
		AshShaderStageFlagBits stage = AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	};
	struct ShaderFullTextResult
	{
		std::string resultShaderText;
		std::string errorMsg;
	};
	class AshDXCContext
	{
	public:
		AshDXCContext() = default;
		~AshDXCContext() = default;
		bool init();
		void uninit();
	public:
		bool preprocess_shader_file_to_full_text(ShaderItem const&  item, ShaderFullTextResult** result);
		void create_compiler_for_target_platform();
	private:
		bool create_blob_from_text(const char* pText, IDxcBlobEncoding** ppBlob);
		bool create_dxc_define_from_user_define(char const* userDefine, std::vector<std::pair<std::wstring, std::wstring>>& dxcDefineStorage, std::vector<DxcDefine>& dxcDefines);
	public:
		Microsoft::WRL::ComPtr<DXCIncludeHandler> get_default_includer();
	private:
		Microsoft::WRL::ComPtr<IDxcLibrary> m_pLibrary = nullptr;
		Microsoft::WRL::ComPtr<IDxcUtils> m_pUtils = nullptr;
		Microsoft::WRL::ComPtr<DXCIncludeHandler> m_pDefaultIncluder = nullptr;
		Microsoft::WRL::ComPtr<IDxcRewriter> m_pRewriter = nullptr;
		Microsoft::WRL::ComPtr<IDxcRewriter2> m_pRewriter2 = nullptr;
	};
	
};
