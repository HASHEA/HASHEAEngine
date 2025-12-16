#include "DXCIncludeHandler.h"
#include "Base/hfile.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include <string>
namespace RHI
{
	DXCIncludeHandler::DXCIncludeHandler()
	{
	}
	DXCIncludeHandler::~DXCIncludeHandler()
	{
		Release();
	}
	HRESULT DXCIncludeHandler::LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource)
	{
		HRESULT               bHRRet = E_FAIL;
		UINT32                codePage = CP_UTF8;
		IDxcBlobEncoding** ppBlob = reinterpret_cast<IDxcBlobEncoding**>(ppIncludeSource);
		std::filesystem::path filePathInclude = pFilename;
		std::filesystem::path findFilePathInclude = {};
		bool fileExist = false;
		//deal with user shader
		if (defaultUserShaderName == filePathInclude.filename())
		{	
			findFilePathInclude = m_currentUserShaderPath;
			fileExist = AshEngine::file_exists(findFilePathInclude.string().c_str());
		}
		else
		{
			std::filesystem::path tryPath = filePathInclude;
			while (!tryPath.empty() && tryPath != ".")
			{
				findFilePathInclude = m_currentIncludeRootPath / tryPath;
				fileExist = AshEngine::file_exists(findFilePathInclude.string().c_str());
				if (fileExist)
					break;
				tryPath = tryPath.lexically_relative(*tryPath.begin());
			}
			if (!fileExist && tryPath == ".")
			{
				findFilePathInclude = m_currentIncludeRootPath / tryPath;
				fileExist = AshEngine::file_exists(findFilePathInclude.string().c_str());
			}
		}
		if (fileExist)
		{
			std::string szWholeFileString{};
			szWholeFileString = AshEngine::file_read_text(findFilePathInclude.string().c_str()).data;
			bHRRet = m_pLibrary->CreateBlobWithEncodingOnHeapCopy(szWholeFileString.data(), static_cast<uint32_t>(szWholeFileString.size()), codePage, ppBlob);
			H_ASSERTLOG(bHRRet == S_OK, "Failed to create blob!");
		}
		else
		{
			HLogError("Load Shader File Not Exist : {}", filePathInclude.string().c_str());
		}
		return S_OK;;
	}
	void DXCIncludeHandler::set_current_user_shader_path(const std::filesystem::path& path)
	{
		m_currentUserShaderPath = path;
	}
	void DXCIncludeHandler::set_include_root_path(const std::filesystem::path& path)
	{
		m_currentIncludeRootPath = path;
	}
	HRESULT __stdcall DXCIncludeHandler::QueryInterface(REFIID riid, void** ppvObject)
	{
		return DoBasicQueryInterface<IDxcIncludeHandler>(this, riid, ppvObject);
	}
	ULONG __stdcall DXCIncludeHandler::AddRef(void)
	{
		return static_cast<ULONG>(++m_dwRef);
	}
	ULONG __stdcall DXCIncludeHandler::Release(void)
	{
		ULONG result = static_cast<ULONG>(--m_dwRef);
		if (result == 0)
		{
			delete this;
		}
		return result;
	}
};