#include "DXCIncludeHandler.h"
#include "Base/hfile.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hmemory.h"
#include <string>
namespace RHI
{
	static inline void free_include_file_text(AshEngine::FileReadResult& result)
	{
		if (result.data)
		{
			AshEngine::MemoryService::instance()->get_system_allocator()->deallocate(result.data);
			result.data = nullptr;
			result.size = 0;
		}
	}

	DXCIncludeHandler::DXCIncludeHandler()
	{
		HRESULT hrRes = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary));
		H_ASSERTLOG(SUCCEEDED(hrRes) && m_pLibrary != nullptr, "Failed to create DXC library for include handler.");
	}
	DXCIncludeHandler::~DXCIncludeHandler()
	{
		if (m_pLibrary)
		{
			m_pLibrary->Release();
			m_pLibrary = nullptr;
		}
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
		else if (defaultGeneratedBindingsName == filePathInclude.filename())
		{
			findFilePathInclude = m_currentGeneratedBindingsPath;
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
			AshEngine::FileReadResult file_text = AshEngine::file_read_text(findFilePathInclude.string().c_str());
			H_ASSERTLOG(file_text.data != nullptr, "Failed to read include file: {}", findFilePathInclude.string().c_str());
			bHRRet = m_pLibrary->CreateBlobWithEncodingOnHeapCopy(file_text.data, static_cast<uint32_t>(file_text.size), codePage, ppBlob);
			free_include_file_text(file_text);
			H_ASSERTLOG(bHRRet == S_OK, "Failed to create blob!");
		}
		else
		{
			HLogError("Load Shader File Not Exist : {}", filePathInclude.string().c_str());
			return E_FAIL;
		}
		return bHRRet;
	}
	void DXCIncludeHandler::set_current_user_shader_path(const std::filesystem::path& path)
	{
		m_currentUserShaderPath = path;
	}
	void DXCIncludeHandler::set_current_generated_bindings_path(const std::filesystem::path& path)
	{
		m_currentGeneratedBindingsPath = path;
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
			this->~DXCIncludeHandler();
			AshEngine::MemoryService::instance()->get_system_allocator()->deallocate(this);
		}
		return result;
	}
};
