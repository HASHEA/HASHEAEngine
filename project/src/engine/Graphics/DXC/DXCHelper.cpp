
#include "DXCIncludeHandler.h"
#include "DXCHelper.h"
#include "Base/hfile.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include <filesystem>
#include <regex>

namespace RHI
{
	static inline std::string wchar_to_utf8(LPCWSTR lpwstr)
	{
		int         len = WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, nullptr, 0, nullptr, nullptr);
		std::string str(len, 0);
		WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, str.data(), len, nullptr, nullptr);
		if (!str.empty() && str.back() == '\0')
			str.pop_back();
		return str;
	}

	// std::string£¨UTF-8£©to std::wstring£¨UTF-16£©
	static inline std::wstring utf8_to_wstring(const std::string& str)
	{
		int          len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
		std::wstring wstr(len, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), len);
		if (!wstr.empty() && wstr.back() == L'\0')
			wstr.pop_back();
		return wstr;
	}

	static std::string blob_to_utf8(IDxcBlob* pBlob)
	{
		HRESULT     hrRes = S_OK;
		std::string retString{};
		if (!pBlob)
			return retString;
		CComPtr<IDxcBlobUtf8>     pBlobUtf8 = nullptr;
		CComPtr<IDxcBlobEncoding> pBlobEncoding = nullptr;

		hrRes = pBlob->QueryInterface(&pBlobUtf8);

		if (hrRes == S_OK)
		{
			retString = std::string(pBlobUtf8->GetStringPointer(), pBlobUtf8->GetStringLength());
		}
		else
		{
			hrRes = pBlob->QueryInterface(&pBlobEncoding);
			H_ASSERTLOG(hrRes == S_OK,"failed to query blob encoding !");

			BOOL   known;
			UINT32 codePage;
			hrRes = pBlobEncoding->GetEncoding(&known, &codePage);
			H_ASSERTLOG(hrRes == S_OK,"failed to GetEncoding !");
			H_ASSERT(known);

			if (codePage == DXC_CP_WIDE)
			{
				const wchar_t* text = static_cast<const wchar_t*>(pBlob->GetBufferPointer());
				size_t         length = pBlob->GetBufferSize() / 2;
				if (length >= 1 && text[length - 1] == L'\0')
					length -= 1; // Exclude null-terminator
				std::wstring wstr(text, length);
				retString = wchar_to_utf8(wstr.data());
			}
			else if (codePage == CP_UTF8)
			{
				const char* text = static_cast<const char*>(pBlob->GetBufferPointer());
				size_t      length = pBlob->GetBufferSize();
				if (length >= 1 && text[length - 1] == '\0')
					length -= 1; // Exclude null-terminator
				retString.resize(length);
				memcpy(retString.data(), text, length);
			}
			else
			{
				H_ASSERT(false);
			}
		}
		return retString;
	}

	static void split_by(char delimiter, char const* content, std::vector<std::string>& result)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR(content);
		ASH_PROCESS_ERROR(!(*content == '\0'));
		std::string_view sv(content);
		size_t start = 0;
		while (start < sv.size()) 
		{
			if (sv[start] == delimiter) {
				start++;
				continue;
			}
			size_t end = sv.find(delimiter, start);
			std::string_view sub = sv.substr(start, end - start);
			result.emplace_back(sub);
			start = (end == std::string_view::npos) ? sv.size() : end + 1;
		}
		ASH_SAFE_EXECUTE_END(bResult);
	}
	bool AshDXCContext::init()
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		if (m_pLibrary == nullptr)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary));
			ASH_PROCESS_ERROR(hrRes);
		}
		if (m_pUtils == nullptr)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_pUtils));
			ASH_PROCESS_ERROR(hrRes);
		}
		if (m_pDefaultIncluder == nullptr)
		{
			m_pDefaultIncluder = AshEngine::Ash_New<DXCIncludeHandler>();
		}
		if (m_pRewriter == nullptr)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcRewriter, IID_PPV_ARGS(&m_pRewriter));
			ASH_PROCESS_ERROR(hrRes);

			hrRes = m_pRewriter->QueryInterface(IID_PPV_ARGS(&m_pRewriter2));
			ASH_PROCESS_ERROR(hrRes);
		}

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	void AshDXCContext::uninit()
	{
		if (m_pLibrary)
		{
			m_pLibrary.Release();
		}
		if (m_pUtils)
		{
			m_pUtils.Release();
		}
		if (m_pDefaultIncluder)
		{
			m_pDefaultIncluder.Release();
		}
		if (m_pRewriter)
		{
			m_pRewriter.Release();
		}
		if (m_pRewriter2)
		{
			m_pRewriter2.Release();
		}
	}
	bool AshDXCContext::preprocess_shader_file_to_full_text(ShaderItem const& item, ShaderFullTextResult** result)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		bool bRetCode = false;
		const char* szShaderSource = AshEngine::file_read_text(item.sourceShaderPath).data;
		const char* entryName = item.entryPoint;
		const char* fileName = item.sourceShaderPath;
		CComPtr<IDxcBlobEncoding> pSourceBlob = nullptr;
		std::filesystem::path shaderPath = fileName;
		bRetCode = create_blob_from_text(szShaderSource, &pSourceBlob);
		ASH_PROCESS_ERROR(bRetCode);
		std::vector<DxcDefine> vecDefines{};
		std::wstring           entryNameW{};
		entryNameW = utf8_to_wstring(entryName);
		bRetCode = create_dxc_define_from_user_define(item.macroDefine, vecDefines);
		ASH_PROCESS_ERROR(bRetCode);
		std::vector<LPCWSTR> rewriteArgs{};
		rewriteArgs.emplace_back(L"-E");
		rewriteArgs.emplace_back(entryNameW.c_str());
		rewriteArgs.emplace_back(L"-remove-unused-globals");
		rewriteArgs.emplace_back(L"-remove-unused-functions");
		//set user shader for includer
		m_pDefaultIncluder->set_current_user_shader_path(item.userShaderPath);
		CComPtr<IDxcOperationResult> pComplResult = nullptr;
		HRESULT hRetCode = m_pRewriter2->RewriteWithOptions(pSourceBlob, shaderPath.filename().c_str(), rewriteArgs.data(), static_cast<uint32_t>(rewriteArgs.size()), 
			vecDefines.data(), static_cast<uint32_t>(vecDefines.size()), m_pDefaultIncluder, &pComplResult);
		ASH_PROCESS_ERROR(hRetCode == S_OK);
		CComPtr<IDxcBlobEncoding>    pErrors = nullptr;
		hRetCode = pComplResult->GetErrorBuffer(&pErrors);
		ASH_PROCESS_ERROR(hRetCode == S_OK);
		ASH_PROCESS_ERROR(*result);
		if (pErrors != nullptr)
		{
			(*result)->errorMsg = blob_to_utf8(pErrors);
		}
		CComPtr<IDxcBlob>            pBlob = nullptr;
		hRetCode = pComplResult->GetResult(&pBlob);
		ASH_PROCESS_ERROR(hRetCode);
		if (pBlob)
		{
			(*result)->resultShaderText = blob_to_utf8(pBlob);
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	void AshDXCContext::create_compiler_for_target_platform()
	{
	}
	bool AshDXCContext::create_blob_from_text(const char* pText, IDxcBlobEncoding** ppBlob)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		bool    bRet = false;
		HRESULT hrRes = E_FAIL;
		hrRes = m_pUtils->CreateBlobFromPinned(pText, static_cast<uint32_t>(strlen(pText) + 1), CP_UTF8, ppBlob);
		ASH_PROCESS_ERROR((hrRes == S_OK));
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	bool AshDXCContext::create_dxc_define_from_user_define(char const* userDefine, std::vector<DxcDefine>& dxcDefines)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		//parse macros with ';'
		std::vector<std::string> vecMacros;
		split_by(';', userDefine, vecMacros);
		for (const auto& macro : vecMacros)
		{
			std::vector<std::string> vecSingleMacro;
			split_by('=', macro.data(), vecSingleMacro);
			H_ASSERTLOG(vecSingleMacro.size() <= 2, "Invalid User Defined Macro Format! The Valid Usage Should Be : _YOUR_MACRO_ = _YOUR_VALUE_ !!");
			ASH_PROCESS_ERROR(vecSingleMacro.size() <= 2);
			std::wstring key = utf8_to_wstring(vecSingleMacro[0]);
			std::wstring value{};

			if (!vecSingleMacro.size() > 1)
			{
				value = utf8_to_wstring(vecSingleMacro[1]);
			}
			dxcDefines.emplace_back(DxcDefine{key.c_str(), value.empty() ? nullptr : value.c_str()});
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	CComPtr<DXCIncludeHandler> AshDXCContext::get_default_includer()
	{
		return m_pDefaultIncluder;
	}
};