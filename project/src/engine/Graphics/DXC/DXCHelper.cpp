
#include "DXCIncludeHandler.h"
#include "DXCHelper.h"
#include "Base/hfile.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include <filesystem>
#include <regex>
#include <unordered_set>

namespace RHI
{
	static inline void free_file_read_result(AshEngine::FileReadResult& result)
	{
		if (result.data)
		{
			AshEngine::MemoryService::instance()->get_system_allocator()->deallocate(result.data);
			result.data = nullptr;
			result.size = 0;
		}
	}

	static inline std::string wchar_to_utf8(LPCWSTR lpwstr)
	{
		int         len = WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, nullptr, 0, nullptr, nullptr);
		std::string str(len, 0);
		WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, str.data(), len, nullptr, nullptr);
		if (!str.empty() && str.back() == '\0')
			str.pop_back();
		return str;
	}

	// Convert UTF-8 std::string to UTF-16 std::wstring.
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

	static size_t find_matching_brace(std::string_view text, size_t openBraceIndex)
	{
		if (openBraceIndex == std::string_view::npos || openBraceIndex >= text.size() || text[openBraceIndex] != '{')
		{
			return std::string_view::npos;
		}

		uint32_t braceDepth = 0;
		for (size_t i = openBraceIndex; i < text.size(); ++i)
		{
			if (text[i] == '{')
			{
				++braceDepth;
			}
			else if (text[i] == '}')
			{
				H_ASSERTLOG(braceDepth > 0, "Invalid brace depth while parsing HLSL type declarations.");
				--braceDepth;
				if (braceDepth == 0)
				{
					return i;
				}
			}
		}

		return std::string_view::npos;
	}

	static bool contains_type_definition(std::string const& shaderText, std::string const& typeName)
	{
		const std::regex structPattern("\\bstruct\\s+" + typeName + "\\b");
		const std::regex classPattern("\\bclass\\s+" + typeName + "\\b");
		const std::regex enumPattern("\\benum(?:\\s+class)?\\s+" + typeName + "\\b");
		return std::regex_search(shaderText, structPattern) ||
			std::regex_search(shaderText, classPattern) ||
			std::regex_search(shaderText, enumPattern);
	}

	static std::vector<std::pair<std::string, std::string>> extract_top_level_type_definitions(std::string_view shaderSource)
	{
		std::vector<std::pair<std::string, std::string>> definitions{};
		const std::regex typePattern(R"(\b(struct|class|enum(?:\s+class)?)\s+([A-Za-z_]\w*)\b)");
		const std::string shaderSourceText(shaderSource);
		auto begin = std::sregex_iterator(shaderSourceText.begin(), shaderSourceText.end(), typePattern);
		auto end = std::sregex_iterator();
		std::unordered_set<std::string> emittedTypeNames{};

		for (auto it = begin; it != end; ++it)
		{
			const std::smatch& match = *it;
			if (match.size() < 3)
			{
				continue;
			}

			std::string typeName = match[2].str();
			if (!emittedTypeNames.insert(typeName).second)
			{
				continue;
			}

			const size_t declarationStart = static_cast<size_t>(match.position(0));
			const size_t searchStart = declarationStart + static_cast<size_t>(match.length(0));
			const size_t braceIndex = shaderSource.find('{', searchStart);
			const size_t declarationSemicolon = shaderSource.find(';', searchStart);
			if (braceIndex == std::string_view::npos ||
				(declarationSemicolon != std::string_view::npos && declarationSemicolon < braceIndex))
			{
				continue;
			}

			const size_t closeBraceIndex = find_matching_brace(shaderSource, braceIndex);
			if (closeBraceIndex == std::string_view::npos)
			{
				continue;
			}

			const size_t trailingSemicolonIndex = shaderSource.find(';', closeBraceIndex);
			if (trailingSemicolonIndex == std::string_view::npos)
			{
				continue;
			}

			definitions.emplace_back(
				std::move(typeName),
				std::string(shaderSource.substr(declarationStart, trailingSemicolonIndex - declarationStart + 1)));
		}

		return definitions;
	}

	static void merge_missing_type_definitions(std::string_view originalShaderSource, std::string& rewrittenShaderText)
	{
		if (rewrittenShaderText.empty())
		{
			return;
		}

		const auto definitions = extract_top_level_type_definitions(originalShaderSource);
		if (definitions.empty())
		{
			return;
		}

		std::string preservedTypes{};
		for (const auto& [typeName, definition] : definitions)
		{
			if (!contains_type_definition(rewrittenShaderText, typeName))
			{
				preservedTypes += definition;
				preservedTypes += "\n\n";
			}
		}

		if (!preservedTypes.empty())
		{
			rewrittenShaderText.insert(0, preservedTypes);
		}
	}

	bool AshDXCContext::init()
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		if (!m_pLibrary)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary));
			ASH_PROCESS_ERROR(SUCCEEDED(hrRes) && static_cast<bool>(m_pLibrary));
		}
		if (!m_pUtils)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_pUtils));
			ASH_PROCESS_ERROR(SUCCEEDED(hrRes) && static_cast<bool>(m_pUtils));
		}
		if (!m_pDefaultIncluder)
		{
			m_pDefaultIncluder.Attach(AshEngine::Ash_New<DXCIncludeHandler>());
			ASH_PROCESS_ERROR(m_pDefaultIncluder.p);
		}
		if (!m_pRewriter)
		{
			HRESULT hrRes = DxcCreateInstance(CLSID_DxcRewriter, IID_PPV_ARGS(&m_pRewriter));
			ASH_PROCESS_ERROR(SUCCEEDED(hrRes) && static_cast<bool>(m_pRewriter));

			hrRes = m_pRewriter->QueryInterface(IID_PPV_ARGS(&m_pRewriter2));
			ASH_PROCESS_ERROR(SUCCEEDED(hrRes) && static_cast<bool>(m_pRewriter2));
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
		AshEngine::FileReadResult shader_source_text = AshEngine::file_read_text(item.sourceShaderPath);
		const char* szShaderSource = shader_source_text.data;
		const char* entryName = item.entryPoint;
		const char* fileName = item.sourceShaderPath;
		CComPtr<IDxcBlobEncoding> pSourceBlob = nullptr;
		std::filesystem::path shaderPath = fileName;
		ASH_PROCESS_ERROR(szShaderSource);
		bRetCode = create_blob_from_text(szShaderSource, &pSourceBlob);
		ASH_PROCESS_ERROR(bRetCode);
		std::vector<std::pair<std::wstring, std::wstring>> defineStorage{};
		std::vector<DxcDefine> vecDefines{};
		std::wstring           entryNameW{};
		entryNameW = utf8_to_wstring(entryName);
		bRetCode = create_dxc_define_from_user_define(item.macroDefine, defineStorage, vecDefines);
		ASH_PROCESS_ERROR(bRetCode);
		std::vector<LPCWSTR> rewriteArgs{};
		rewriteArgs.emplace_back(L"-E");
		rewriteArgs.emplace_back(entryNameW.c_str());
		//set user shader for includer
		if (item.userShaderPath && *item.userShaderPath != '\0')
		{
			m_pDefaultIncluder.p->set_current_user_shader_path(item.userShaderPath);
		}
		else
		{
			m_pDefaultIncluder.p->set_current_user_shader_path(std::filesystem::path{});
		}
		if (item.generatedBindingsPath && *item.generatedBindingsPath != '\0')
		{
			m_pDefaultIncluder.p->set_current_generated_bindings_path(item.generatedBindingsPath);
		}
		else
		{
			m_pDefaultIncluder.p->set_current_generated_bindings_path(std::filesystem::path{});
		}
		m_pDefaultIncluder.p->set_include_root_path(shaderPath.parent_path());
		CComPtr<IDxcOperationResult> pComplResult = nullptr;
		HRESULT hRetCode = m_pRewriter2->RewriteWithOptions(pSourceBlob, shaderPath.filename().c_str(), rewriteArgs.data(), static_cast<uint32_t>(rewriteArgs.size()), 
			vecDefines.data(), static_cast<uint32_t>(vecDefines.size()), m_pDefaultIncluder.p, &pComplResult);
		ASH_PROCESS_ERROR(hRetCode == S_OK);
		CComPtr<IDxcBlobEncoding>    pErrors = nullptr;
		hRetCode = pComplResult->GetErrorBuffer(&pErrors);
		ASH_PROCESS_ERROR(hRetCode == S_OK);
		ASH_PROCESS_ERROR(*result);
		if (pErrors)
		{
			(*result)->errorMsg = blob_to_utf8(pErrors);
		}
		CComPtr<IDxcBlob>            pBlob = nullptr;
		hRetCode = pComplResult->GetResult(&pBlob);
		ASH_PROCESS_ERROR(SUCCEEDED(hRetCode));
		if (pBlob)
		{
			(*result)->resultShaderText = blob_to_utf8(pBlob);
			merge_missing_type_definitions(szShaderSource, (*result)->resultShaderText);
		}
		free_file_read_result(shader_source_text);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	void AshDXCContext::create_compiler_for_target_platform()
	{
	}
	bool AshDXCContext::create_blob_from_text(const char* pText, IDxcBlobEncoding** ppBlob)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR(m_pUtils.p);
		ASH_PROCESS_ERROR(pText);
		ASH_PROCESS_ERROR(ppBlob);
		HRESULT hrRes = E_FAIL;
		hrRes = m_pUtils->CreateBlobFromPinned(pText, static_cast<uint32_t>(strlen(pText) + 1), CP_UTF8, ppBlob);
		ASH_PROCESS_ERROR(SUCCEEDED(hrRes) && *ppBlob);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	bool AshDXCContext::create_dxc_define_from_user_define(char const* userDefine, std::vector<std::pair<std::wstring, std::wstring>>& dxcDefineStorage, std::vector<DxcDefine>& dxcDefines)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		if (userDefine == nullptr || *userDefine == '\0')
		{
			return true;
		}
		//parse macros with ';'
		std::vector<std::string> vecMacros;
		split_by(';', userDefine, vecMacros);
		dxcDefineStorage.reserve(vecMacros.size());
		dxcDefines.reserve(vecMacros.size());
		for (const auto& macro : vecMacros)
		{
			std::vector<std::string> vecSingleMacro;
			split_by('=', macro.data(), vecSingleMacro);
			H_ASSERTLOG(vecSingleMacro.size() <= 2, "Invalid User Defined Macro Format! The Valid Usage Should Be : _YOUR_MACRO_ = _YOUR_VALUE_ !!");
			ASH_PROCESS_ERROR(vecSingleMacro.size() <= 2);
			std::wstring key = utf8_to_wstring(vecSingleMacro[0]);
			std::wstring value{};

			if (vecSingleMacro.size() > 1)
			{
				value = utf8_to_wstring(vecSingleMacro[1]);
			}
			dxcDefineStorage.emplace_back(std::move(key), std::move(value));
			const auto& definePair = dxcDefineStorage.back();
			dxcDefines.emplace_back(DxcDefine{ definePair.first.c_str(), definePair.second.empty() ? nullptr : definePair.second.c_str() });
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	CComPtr<DXCIncludeHandler> AshDXCContext::get_default_includer()
	{
		return m_pDefaultIncluder;
	}
};
