#include "KGFX_DXCComplieDx12.h"
#include <atlcomcli.h>
#include <d3d12shader.h>
#include <filesystem>
#include <regex>
#include <wrl/client.h>

#include "KGFX_ShaderResourceDx12.h"
#include "KEnginePub/Private/comm/KGFX_ShaderHelper.h"
#include "KBase/Public/KMemLeak.h"
#include "KBase/Public/memory/KBufferReader.h"

namespace gfx
{
    inline std::string WCharToUtf8(LPCWSTR lpwstr)
    {
        int         len = WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, nullptr, 0, nullptr, nullptr);
        std::string str(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, lpwstr, -1, str.data(), len, nullptr, nullptr);
        // 去掉末尾的 '\0'
        if (!str.empty() && str.back() == '\0')
            str.pop_back();
        return str;
    }

    // std::string（UTF-8）转 std::wstring（UTF-16）
    static inline std::wstring Utf8ToWString(const std::string& str)
    {
        int          len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), len);
        // 去掉末尾的 '\0'
        if (!wstr.empty() && wstr.back() == L'\0')
            wstr.pop_back();
        return wstr;
    }

    static std::string BlobToUtf8(IDxcBlob* pBlob)
    {
        HRESULT     hrRes = S_OK;
        std::string retString = {};

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
            KGLOG_COM_PROCESS_ERROR(hrRes);

            BOOL   known;
            UINT32 codePage;
            hrRes = pBlobEncoding->GetEncoding(&known, &codePage);
            KGLOG_COM_PROCESS_ERROR(hrRes);
            assert(known);

            if (codePage == DXC_CP_WIDE)
            {
                const wchar_t* text = static_cast<const wchar_t*>(pBlob->GetBufferPointer());
                size_t         length = pBlob->GetBufferSize() / 2;
                if (length >= 1 && text[length - 1] == L'\0')
                    length -= 1; // Exclude null-terminator
                std::wstring wstr(text, length);
                retString = WCharToUtf8(wstr.data());
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
                assert(false);
            }
        }

    Exit0:
        return retString;
    }


    SimpleIncludeHanlder::SimpleIncludeHanlder(IDxcLibrary* pLibrary)
    {
        m_pLibrary = pLibrary;
        if (m_pLibrary)
        {
            m_pLibrary->AddRef();
        }
    }

    SimpleIncludeHanlder::~SimpleIncludeHanlder()
    {
        SAFE_RELEASE(m_pLibrary);
    }

    ULONG SimpleIncludeHanlder::AddRef() noexcept
    {
        return static_cast<ULONG>(++m_dwRef);
    }

    ULONG SimpleIncludeHanlder::Release()
    {
        ULONG result = static_cast<ULONG>(--m_dwRef);
        if (result == 0)
        {
            delete this;
        }
        return result;
    }

    HRESULT SimpleIncludeHanlder::QueryInterface(const IID& riid, void** ppvObject)
    {
        return DoBasicQueryInterface<IDxcIncludeHandler>(this, riid, ppvObject);
    }

    HRESULT SimpleIncludeHanlder::LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource)
    {
        HRESULT               bHRRet = E_FAIL;
        UINT32                codePage = CP_UTF8;
        std::string           szWholeFileString = {};
        IDxcBlobEncoding** ppBlob = reinterpret_cast<IDxcBlobEncoding**>(ppIncludeSource);
        std::filesystem::path filePathInclude = pFilename;
        std::filesystem::path findFilePathInclude = {};
        KUniqueStr            ustrShaderPath = {};
        bool                  bExist = false;
        bool bMatPack = false;
        KGFX_ShaderFilePool* pShaderFilePool = KGFX_GetShaderFilePool();
        NSKBase::tagFileLocation sShaderLoc;

        if (defaultUserShaderName == filePathInclude.filename())
        {
            findFilePathInclude = m_UserShaderPath;
            ustrShaderPath = g_CachePathString(findFilePathInclude.string().c_str(), TRUE);
            bMatPack = findFilePathInclude.string().find(".matpack") != std::string::npos;
            if (bMatPack)
            {
                sShaderLoc = GetPackUserShaderLocation();
                bExist = sShaderLoc.FileExist();
            }
            else
            {
                bExist = KGFExist(ustrShaderPath);
            }
        }
        else
        {
            // 不断去掉第一个subpath，尝试查找
            std::filesystem::path tryPath = filePathInclude;

            while (!tryPath.empty() && tryPath != ".")
            {
                findFilePathInclude = m_ShaderIncludeRootPath / tryPath;
                ustrShaderPath = g_CachePathString(findFilePathInclude.string().c_str(), TRUE);
                bExist = KGFExist(ustrShaderPath);
                if (bExist)
                    break;
                // 去掉第一个subpath
                tryPath = tryPath.lexically_relative(*tryPath.begin());
            }

            // 循环后再尝试一次 "."
            if (!bExist && tryPath == ".")
            {
                findFilePathInclude = m_ShaderIncludeRootPath / tryPath;
                ustrShaderPath = g_CachePathString(findFilePathInclude.string().c_str(), TRUE);
                bExist = KGFExist(ustrShaderPath);
            }
        }

        if (bExist)
        {

            if (!bMatPack)
            {
                sShaderLoc = NSKBase::tagFileLocation{ ustrShaderPath };
            }

            KAutoRefPtr<KGFX_ShaderFile> readFile = {};
            readFile.Attach(pShaderFilePool->OnlyLoadShaderFile(sShaderLoc, nullptr), {});
            szWholeFileString = readFile->GetFileContent();
            bHRRet = m_pLibrary->CreateBlobWithEncodingOnHeapCopy(szWholeFileString.data(), static_cast<uint32_t>(szWholeFileString.size()), codePage, ppBlob);
            KGLOG_COM_PROCESS_ERROR(bHRRet);
        }
        else
        {
            KGLogPrintf(KGLOG_ERR, "[LOAD SHADER FILE NOT EXIST:]%s", filePathInclude.string().c_str());
        }

        bHRRet = S_OK;
    Exit0:
        return bHRRet;
    }

    void SimpleIncludeHanlder::SetUserShaderName(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            m_UserShaderPath = defaultUserShaderPath;
        }
        else
        {
            m_UserShaderPath = path;
        }
    }

    void SimpleIncludeHanlder::SetIncludeRootPath(const std::filesystem::path& path)
    {
        m_ShaderIncludeRootPath = path;
    }

    NSKBase::tagFileLocation SimpleIncludeHanlder::GetPackUserShaderLocation() const
    {

        std::string s = m_UserShaderPath.string();
        std::stringstream ss(s);
        std::string delimiter = "<=>";
        std::vector<std::string> result;
        size_t start = 0;
        size_t end = 0;
        while ((end = s.find(delimiter, start)) != std::string::npos)
        {
            result.push_back(s.substr(start, end - start));
            start = end + delimiter.length();
        }
        result.push_back(s.substr(start));
        assert(result.size() == 2);

        NSKBase::tagFileLocation packFileReader = { result.at(0).c_str(),result.at(1).c_str() };

        return packFileReader;
    }

    KGFX_DXCComplierDX12::~KGFX_DXCComplierDX12()
    {
        Reset();
    }

    bool KGFX_DXCComplierDX12::Init()
    {
        bool    bRet = false;
        HRESULT hrRes = E_FAIL;
        if (m_pCompiler == nullptr)
        {
            hrRes = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_pCompiler));
            KGLOG_COM_PROCESS_ERROR(hrRes);

            hrRes = m_pCompiler->QueryInterface(&m_pCompiler3);
            KGLOG_COM_PROCESS_ERROR(hrRes);

            HRESULT hresult = m_pCompiler->QueryInterface(&m_pDxcVersionInfo);
            assert(hresult == S_OK);

            hresult = m_pDxcVersionInfo->QueryInterface(&m_pDxcVersionInfo3);
            assert(hresult == S_OK);

            char* versionInfo = nullptr;
            m_pDxcVersionInfo3->GetCustomVersionString(&versionInfo);
            m_DxcVersion = versionInfo;
            CoTaskMemFree(versionInfo);
        }

        if (m_pLibrary == nullptr)
        {
            hrRes = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_pLibrary));
            KGLOG_COM_PROCESS_ERROR(hrRes);
        }

        if (m_pUtils == nullptr)
        {
            hrRes = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_pUtils));
            KGLOG_COM_PROCESS_ERROR(hrRes);
        }

        if (m_pIncludeHandler == nullptr)
        {
            m_pIncludeHandler = new SimpleIncludeHanlder(m_pLibrary);
        }

        if (m_pRewriter == nullptr)
        {
            hrRes = DxcCreateInstance(CLSID_DxcRewriter, IID_PPV_ARGS(&m_pRewriter));
            KGLOG_COM_PROCESS_ERROR(hrRes);

            hrRes = m_pRewriter->QueryInterface(&m_pRewriter2);
            KGLOG_COM_PROCESS_ERROR(hrRes);
        }

        if (m_pShaderCacheManager == nullptr)
        {
            m_pShaderCacheManager.Attch(new PersistentCache());
            KGLOG_PROCESS_ERROR(m_pShaderCacheManager);

            m_pShaderCacheManager->Initialize();
        }

        InitArg();
        bRet = true;
    Exit0:
        return bRet;
    }

    void KGFX_DXCComplierDX12::Reset()
    {
        SAFE_RELEASE(m_pIncludeHandler);
    }


    bool KGFX_DXCComplierDX12::CompileShader(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg)
    {
        bool                            bRet = false;
        HRESULT                         hrRes = E_FAIL;
        CComPtr<IDxcBlobEncoding>       pSource = nullptr;
        CComPtr<IDxcOperationResult>    pComplResult = nullptr;
        CComPtr<IDxcBlob>               pBlob = nullptr;
        CComPtr<IDxcBlobEncoding>       pErrors = nullptr;
        DxcBuffer                       buf = {};
        CComPtr<ID3D12ShaderReflection> pReflection = nullptr;

        const char* szShaderSource = pitem->m_pMainShaderFile->GetFileContent();
        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto                                               gfxStage = pitem->m_ShageStage;
        // auto gfxStage = ChangeStage(stageOld);
        auto                                               dxcMacro = pitem->m_TechMacroDXC;
        std::vector<std::pair<std::wstring, std::wstring>> dxcWMacro = {};

        std::filesystem::path shaderPath = fileName;
        assert(szShaderSource);
        assert(entryName);
        assert(fileName);

        std::wstring proFile = {};
        const auto& items = ShaderStageType_info::items();

        auto it = std::find_if(items.begin(), items.end(), [gfxStage](const auto& item) {
            return item.first == gfxStage;
            });

        auto        proVer = m_ProfileVersion;
        const auto& versionItems = ProfileVersion_info::items();
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
            });

        proFile = Utf8ToWString(it->second) + Utf8ToWString(itV->second);

        bRet = CreateBlobFromText(szShaderSource, &pSource);
        assert(bRet);

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        for (const auto& macro : dxcMacro)
        {
            std::wstring key = Utf8ToWString(macro.first);
            std::wstring value = {};

            if (!macro.second.empty())
            {
                value = Utf8ToWString(macro.second);
            }

            dxcWMacro.emplace_back(key, value);
        }

        for (const auto& wmacro : dxcWMacro)
        {
            DxcDefine dxcDefine = {};
            dxcDefine.Name = wmacro.first.c_str();
            dxcDefine.Value = wmacro.second.empty() ? nullptr : wmacro.second.c_str();
            pDefines.emplace_back(dxcDefine);
        }

        hrRes = m_pCompiler->Compile(pSource, sourceName.c_str(), entryNameW.data(), proFile.data(), m_ComplierArgs.data(), static_cast<uint32_t>(m_ComplierArgs.size()), pDefines.data(), static_cast<uint32_t>(pDefines.size()), m_pIncludeHandler, &pComplResult);
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetResult(&pBlob);
        assert(hrRes == S_OK);

        if (hrRes == S_OK)
        {
            hrRes = pComplResult->GetErrorBuffer(&pErrors);
            assert(hrRes == S_OK);

            outErrorMsg = BlobToUtf8(pErrors);
        }

        buf.Ptr = pBlob->GetBufferPointer();
        buf.Size = pBlob->GetBufferSize();


        hrRes = m_pUtils->CreateReflection(&buf, IID_PPV_ARGS(&pReflection));
        KGLOG_COM_PROCESS_ERROR(hrRes);

        *outRefl = pReflection.Detach();
        *outBlob = pBlob.Detach();
        bRet = true;

    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::CompileShader3(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    {
        bool                            bRet = false;
        bool                            bRes = false;
        HRESULT                         hrRes = E_FAIL;
        CComPtr<IDxcOperationResult>    pComplResult = nullptr;
        CComPtr<IDxcBlob>               pBlob = nullptr;
        CComPtr<IDxcBlobEncoding>       pErrors = nullptr;
        DxcBuffer                       shaderSource = {};
        DxcBuffer                       buildRes = {};
        CComPtr<ID3D12ShaderReflection> pReflection = nullptr;

        const char* szShaderSource = pitem->m_pMainShaderFile->GetFileContent();
        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto        gfxStage = pitem->m_ShageStage;
        auto        dxcMacro = pitem->m_TechMacroDXC;

        std::filesystem::path shaderPath = fileName;
        assert(szShaderSource);
        assert(entryName);
        assert(fileName);

        std::wstring proFile = {};
        const auto& items = ShaderStageType_info::items();

        auto it = std::find_if(items.begin(), items.end(), [gfxStage](const auto& item) {
            return item.first == gfxStage;
            });

        auto        proVer = m_ProfileVersion;
        const auto& versionItems = ProfileVersion_info::items();
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
            });

        proFile = Utf8ToWString(it->second) + Utf8ToWString(itV->second);


        shaderSource.Ptr = pitem->m_pMainShaderFile->GetFileContent();
        shaderSource.Size = pitem->m_pMainShaderFile->GetFileContentSize();
        shaderSource.Encoding = CP_UTF8;

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        std::vector<std::pair<std::wstring, std::wstring>> defineWMarco = {};
        pDefines = CreateBuildMarcoDefine(dxcMacro, defineWMarco);
        auto processArgs = CreateComplierArgs(sourceName, entryNameW, proFile, m_ComplierArgs, pDefines);

        hrRes = m_pCompiler3->Compile(&shaderSource, processArgs->GetArguments(), processArgs->GetCount(), m_pIncludeHandler, IID_PPV_ARGS(&pComplResult));
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetErrorBuffer(&pErrors);
        assert(hrRes == S_OK);

        if (pErrors != nullptr)
        {
            outErrorMsg = BlobToUtf8(pErrors);
        }

        hrRes = pComplResult->GetResult(&pBlob);
        assert(hrRes == S_OK);

        bRes = BuildDXReflection(pBlob, &pReflection);
        assert(bRes);
        KG_PROCESS_ERROR(bRes);

        *outRefl = pReflection.Detach();
        *outBlob = pBlob.Detach();

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::CompileShaderFullText3(const DxcBuffer& shaderSource, const KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    {
        HRESULT hrRes = E_FAIL;
        bool    bRes = false;
        bool    bRet = false;

        CComPtr<IDxcBlobEncoding>       pErrors = nullptr;
        CComPtr<IDxcOperationResult>    pComplResult = nullptr;
        CComPtr<IDxcBlob>               pBlob = nullptr;
        CComPtr<IDxcCompilerArgs>       processArgs = nullptr;
        CComPtr<ID3D12ShaderReflection> pReflection = nullptr;

        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto                  gfxStage = pitem->m_ShageStage;
        std::filesystem::path shaderPath = fileName;

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        std::wstring proFile = {};
        const auto& items = ShaderStageType_info::items();

        auto it = std::find_if(items.begin(), items.end(), [gfxStage](const auto& item) {
            return item.first == gfxStage;
            });

        auto        proVer = m_ProfileVersion;
        const auto& versionItems = ProfileVersion_info::items();
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
            });

        proFile = Utf8ToWString(it->second) + Utf8ToWString(itV->second);

        processArgs = CreateComplierArgs(sourceName, entryNameW, proFile, m_ComplierArgs, pDefines);

        hrRes = m_pCompiler3->Compile(&shaderSource, processArgs->GetArguments(), processArgs->GetCount(), m_pIncludeHandler, IID_PPV_ARGS(&pComplResult));
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetErrorBuffer(&pErrors);
        assert(hrRes == S_OK);

        if (pErrors != nullptr)
        {
            outErrorMsg = BlobToUtf8(pErrors);
        }

        hrRes = pComplResult->GetResult(&pBlob);
        assert(hrRes == S_OK);

        bRes = BuildDXReflection(pBlob, &pReflection);
        assert(bRes);
        KG_PROCESS_ERROR(bRes);

        *outRefl = pReflection.Detach();
        *outBlob = pBlob.Detach();

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::CompileLibShaderFullText3(const DxcBuffer& shaderSource, const KGFX_ShaderTechItemDX12* pitem, ID3D12LibraryReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    {
        HRESULT hrRes = E_FAIL;
        bool    bRes = false;
        bool    bRet = false;

        CComPtr<IDxcBlobEncoding>       pErrors = nullptr;
        CComPtr<IDxcOperationResult>    pComplResult = nullptr;
        CComPtr<IDxcBlob>               pBlob = nullptr;
        CComPtr<IDxcCompilerArgs>       processArgs = nullptr;
        CComPtr<ID3D12LibraryReflection> pReflection = nullptr;

        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto                  gfxStage = pitem->m_ShageStage;
        std::filesystem::path shaderPath = fileName;

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        std::wstring proFile = {};
        const auto& items = ShaderStageType_info::items();

        auto it = std::find_if(items.begin(), items.end(), [gfxStage](const auto& item) {
            return item.first == gfxStage;
        });

        auto        proVer = m_ProfileVersion;
        const auto& versionItems = ProfileVersion_info::items();
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
        });

        proFile = Utf8ToWString(it->second) + Utf8ToWString(itV->second);

        processArgs = CreateComplierArgs(sourceName, entryNameW, proFile, m_ComplierArgs, pDefines);

        hrRes = m_pCompiler3->Compile(&shaderSource, processArgs->GetArguments(), processArgs->GetCount(), m_pIncludeHandler, IID_PPV_ARGS(&pComplResult));
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetErrorBuffer(&pErrors);
        assert(hrRes == S_OK);

        if (pErrors != nullptr)
        {
            outErrorMsg = BlobToUtf8(pErrors);
        }

        hrRes = pComplResult->GetResult(&pBlob);
        assert(hrRes == S_OK);

        bRes = BuildDXLibReflection(pBlob, &pReflection);
        assert(bRes);
        KG_PROCESS_ERROR(bRes);

        *outRefl = pReflection.Detach();
        *outBlob = pBlob.Detach();

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::PreprocessShader(const KGFX_ShaderTechItemDX12* pitem, std::string& outShader, std::string& outErrorMsg) const
    {
        bool                         bRet = false;
        HRESULT                      hrRes = E_FAIL;
        CComPtr<IDxcOperationResult> pComplResult = nullptr;
        CComPtr<IDxcBlob>            pBlob = nullptr;
        CComPtr<IDxcBlobEncoding>    pErrors = nullptr;
        CComPtr<IDxcCompilerArgs>    processArgs = nullptr;
        DxcBuffer                    shaderSource = {};

        const char* szShaderSource = pitem->m_pMainShaderFile->GetFileContent();
        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto        dxcMacro = pitem->m_TechMacroDXC;

        std::filesystem::path shaderPath = fileName;
        assert(szShaderSource);
        assert(entryName);
        assert(fileName);

        shaderSource.Ptr = pitem->m_pMainShaderFile->GetFileContent();
        shaderSource.Size = pitem->m_pMainShaderFile->GetFileContentSize();
        shaderSource.Encoding = CP_UTF8;

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        std::vector<std::pair<std::wstring, std::wstring>> defineWMarco = {};
        pDefines = CreateBuildMarcoDefine(dxcMacro, defineWMarco);
        processArgs = CreatePreprocessArgs(sourceName, entryNameW, pDefines);

        hrRes = m_pCompiler3->Compile(&shaderSource, processArgs->GetArguments(), processArgs->GetCount(), m_pIncludeHandler, IID_PPV_ARGS(&pComplResult));
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetErrorBuffer(&pErrors);
        assert(hrRes == S_OK);

        if (pErrors != nullptr)
        {
            outErrorMsg = BlobToUtf8(pErrors);
        }

        hrRes = pComplResult->GetResult(&pBlob);
        KGLOG_COM_PROCESS_ERROR(hrRes);
        assert(pBlob);

        outShader = BlobToUtf8(pBlob);

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::PreprocessShaderRemoveUnused(KGFX_ShaderTechItemDX12* pitem, std::string& outShader, std::string& outErrorMsg) const
    {
        bool                         bRet = false;
        HRESULT                      hrRes = E_FAIL;
        CComPtr<IDxcBlobEncoding>    pSource = nullptr;
        CComPtr<IDxcOperationResult> pComplResult = nullptr;
        CComPtr<IDxcBlob>            pBlob = nullptr;
        CComPtr<IDxcBlobEncoding>    pErrors = nullptr;

        const char* szShaderSource = pitem->m_pMainShaderFile->GetFileContent();
        const char* entryName = pitem->m_EntryPoint.c_str();
        const char* fileName = pitem->m_pMainShaderFile->GetKey();
        auto dxcMacro = pitem->m_TechMacroDXC;
        std::vector<std::pair<std::wstring, std::wstring>> dxcWMacro = {};

        std::filesystem::path shaderPath = fileName;
        assert(szShaderSource);
        assert(entryName);
        assert(fileName);

        bool bRes = CreateBlobFromText(szShaderSource, &pSource);
        assert(bRes);

        std::vector<DxcDefine> pDefines = {};
        std::wstring           entryNameW = {};
        entryNameW = Utf8ToWString(entryName);
        std::wstring sourceName = shaderPath.filename().wstring();

        std::vector<std::pair<std::wstring, std::wstring>> defineWMarco = {};
        pDefines = CreateBuildMarcoDefine(dxcMacro, defineWMarco);
        auto rewriteArgs = CreateRewriteArgs(entryNameW);

        // m_pIncludeHandler->SetUserShaderName()
        hrRes = m_pRewriter2->RewriteWithOptions(pSource, sourceName.c_str(), rewriteArgs.data(), static_cast<uint32_t>(rewriteArgs.size()), pDefines.data(), static_cast<uint32_t>(pDefines.size()), m_pIncludeHandler, &pComplResult);
        assert(hrRes == S_OK);

        hrRes = pComplResult->GetErrorBuffer(&pErrors);
        assert(hrRes == S_OK);

        if (pErrors != nullptr)
        {
            outErrorMsg = BlobToUtf8(pErrors);
        }

        hrRes = pComplResult->GetResult(&pBlob);
        KGLOG_COM_PROCESS_ERROR(hrRes);
        assert(pBlob);

        outShader = BlobToUtf8(pBlob);

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::CheckShaderCache(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    {
        bool                            bRes = false;
        bool                            bNeedRebuild = false;
        DigestBuilder<SHA1>             passkeybuilder = {};
        DigestBuilder<SHA1>             shaderTextbuilder = {};
        CComPtr<IDxcBlobEncoding>       pBlobEncoding = nullptr;
        RefPtr<KGFX_IBlob>              shaderCacheBlob = nullptr;
        CComPtr<ID3D12ShaderReflection> pCacheRefl = nullptr;
        outErrorMsg = {};
        /// pass的关键字
        std::string passKey = pitem->m_Key;

        /// 宏的关键字
        for (const auto& key : pitem->m_TechMacroDXC)
        {
            passKey += key.first + key.second;
        }

        std::wstring bulidArg = {};
        for (auto& eachArg : m_ComplierArgs)
        {
            bulidArg += eachArg;
        }

        const auto& versionItems = ProfileVersion_info::items();

        auto        proVer = m_ProfileVersion;
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
            });

        std::string shaderMoudleVersion = itV->second;

        passkeybuilder.Append(passKey);
        HashDigest<20> passKeyHash = passkeybuilder.finalize();
        HashDigest<20> textKeyHash = {};
        std::string    outShader = {};
        std::string    outError = {};

        m_pIncludeHandler->SetUserShaderName(pitem->m_UserShaderPath);
        if constexpr (m_bUseRewrite)
        {
            bRes = PreprocessShaderRemoveUnused(pitem, outShader, outError);
            KGLOG_PROCESS_ERROR(bRes);
        }
        else
        {
            bRes = PreprocessShader(pitem, outShader, outError);
            KGLOG_PROCESS_ERROR(bRes);
        }


        shaderTextbuilder.Append(outShader);
        shaderTextbuilder.Append(bulidArg);
        shaderTextbuilder.Append(m_DxcVersion);
        shaderTextbuilder.Append(shaderMoudleVersion);
        textKeyHash = shaderTextbuilder.finalize();

        bRes = m_pShaderCacheManager->ReadEntry(passKeyHash, textKeyHash, &shaderCacheBlob);


        if (bRes && shaderCacheBlob != nullptr)
        {
            /// 缓存存在并且有数据，接下来解析反射，看看数据是否正确
            m_pUtils->CreateBlob(shaderCacheBlob->GetBufferPointer(), static_cast<uint32_t>(shaderCacheBlob->GetBufferSize()), CP_ACP, &pBlobEncoding);
            bRes = BuildDXReflection(pBlobEncoding, &pCacheRefl);
            assert(bRes);

            bNeedRebuild = !bRes;
        }
        else
        {
            /// 缓存不存在或者失效，写入新的文件
            bNeedRebuild = true;
        }

        if (bNeedRebuild)
        {
            DxcBuffer fullText = {};
            fullText.Ptr = outShader.data();
            fullText.Size = outShader.size();
            fullText.Encoding = CP_UTF8;

            bRes = CompileShaderFullText3(fullText, pitem, &pCacheRefl, reinterpret_cast<IDxcBlob**>(&pBlobEncoding), outErrorMsg);
            KG_PROCESS_ERROR(bRes);

            bool bError = CheckShaderHasErrorMesg(outErrorMsg);

            if (!bError)
            {
                shaderCacheBlob.Attch(RawBlob::Create(pBlobEncoding->GetBufferPointer(), pBlobEncoding->GetBufferSize()));
                m_pShaderCacheManager->WriteEntry(passKeyHash, textKeyHash, shaderCacheBlob);
            }
        }

        *outRefl = pCacheRefl.Detach();
        *outBlob = pBlobEncoding.Detach();

    Exit0:
        outErrorMsg += outError;
        return true;
    }

    bool KGFX_DXCComplierDX12::CheckLibShaderCache(KGFX_ShaderTechItemDX12* pitem, ID3D12LibraryReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    {
        bool                            bRes = false;
        bool                            bNeedRebuild = false;
        DigestBuilder<SHA1>             passkeybuilder = {};
        DigestBuilder<SHA1>             shaderTextbuilder = {};
        CComPtr<IDxcBlobEncoding>       pBlobEncoding = nullptr;
        RefPtr<KGFX_IBlob>              shaderCacheBlob = nullptr;
        CComPtr<ID3D12LibraryReflection> pCacheRefl = nullptr;
        outErrorMsg = {};
        /// pass的关键字
        std::string passKey = pitem->m_Key;

        /// 宏的关键字
        for (const auto& key : pitem->m_TechMacroDXC)
        {
            passKey += key.first + key.second;
        }

        std::wstring bulidArg = {};
        for (auto& eachArg : m_ComplierArgs)
        {
            bulidArg += eachArg;
        }

        const auto& versionItems = ProfileVersion_info::items();

        auto        proVer = m_ProfileVersion;
        auto        itV = std::find_if(versionItems.begin(), versionItems.end(), [proVer](const auto& item) {
            return item.first == proVer;
        });

        std::string shaderMoudleVersion = itV->second;

        passkeybuilder.Append(passKey);
        HashDigest<20> passKeyHash = passkeybuilder.finalize();
        HashDigest<20> textKeyHash = {};
        std::string    outShader = {};
        std::string    outError = {};

        m_pIncludeHandler->SetUserShaderName(pitem->m_UserShaderPath);
      /*  if constexpr (false)
        {
            bRes = PreprocessShaderRemoveUnused(pitem, outShader, outError);
            KGLOG_PROCESS_ERROR(bRes);
        }
        else
        {*/
        bRes = PreprocessShader(pitem, outShader, outError);
        KGLOG_PROCESS_ERROR(bRes);
        //}


        shaderTextbuilder.Append(outShader);
        shaderTextbuilder.Append(bulidArg);
        shaderTextbuilder.Append(m_DxcVersion);
        shaderTextbuilder.Append(shaderMoudleVersion);
        textKeyHash = shaderTextbuilder.finalize();

        bRes = m_pShaderCacheManager->ReadEntry(passKeyHash, textKeyHash, &shaderCacheBlob);


        if (bRes && shaderCacheBlob != nullptr)
        {
            /// 缓存存在并且有数据，接下来解析反射，看看数据是否正确
            m_pUtils->CreateBlob(shaderCacheBlob->GetBufferPointer(), static_cast<uint32_t>(shaderCacheBlob->GetBufferSize()), CP_ACP, &pBlobEncoding);
            bRes = BuildDXLibReflection(pBlobEncoding, &pCacheRefl);
            assert(bRes);

            bNeedRebuild = !bRes;
        }
        else
        {
            /// 缓存不存在或者失效，写入新的文件
            bNeedRebuild = true;
        }

        if (bNeedRebuild)
        {
            DxcBuffer fullText = {};
            fullText.Ptr = outShader.data();
            fullText.Size = outShader.size();
            fullText.Encoding = CP_UTF8;

            bRes = CompileLibShaderFullText3(fullText, pitem, &pCacheRefl, reinterpret_cast<IDxcBlob**>(&pBlobEncoding), outErrorMsg);
            KG_PROCESS_ERROR(bRes);

            bool bError = CheckShaderHasErrorMesg(outErrorMsg);

            if (!bError)
            {
                shaderCacheBlob.Attch(RawBlob::Create(pBlobEncoding->GetBufferPointer(), pBlobEncoding->GetBufferSize()));
                m_pShaderCacheManager->WriteEntry(passKeyHash, textKeyHash, shaderCacheBlob);
            }
        }

        *outRefl = pCacheRefl.Detach();
        *outBlob = pBlobEncoding.Detach();

    Exit0:
        outErrorMsg += outError;
        return true;
    }

    static const std::filesystem::path& getProjectDirectory()
    {
        static std::filesystem::path directory(L"H:/Sword3DX/code/sword3-products/trunk/client/enginedata/material/shader_dx12/src/");
        return directory;
    }

    //bool KGFX_DXCComplierDX12::CompileShaderLink(const KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const
    //{
    //    struct FileWithBlob
    //    {
    //        IDxcBlobEncoding* BlobEncoding;

    //        FileWithBlob(IDxcLibrary* pLibrary, LPCWSTR path)
    //        {
    //            UINT32 codePage = CP_UTF8;
    //            pLibrary->CreateBlobFromFile(path, &codePage, &BlobEncoding);
    //        }
    //    };

    //    HRESULT               hrRes = E_FAIL;
    //    std::filesystem::path entryFile = getProjectDirectory() / L"test12.hlsl";
    //    std::filesystem::path includeFile = getProjectDirectory() / L"VSInput2.hlsl";

    //    FileWithBlob source(m_pLibrary, (entryFile.c_str()));

    //    FileWithBlob sourceInclude(m_pLibrary, (includeFile.c_str()));

    //    std::vector          argsLink = { L"-auto-binding-space", L"0" };
    //    IDxcOperationResult* pResultLib1 = nullptr;
    //    IDxcBlob* pResLib1 = nullptr;
    //    m_pCompiler->Compile(source.BlobEncoding, entryFile.c_str(), L"", L"lib_6_6", argsLink.data(), static_cast<uint32_t>(argsLink.size()), nullptr, 0, m_pIncludeHandler, &pResultLib1);

    //    HRESULT status;
    //    pResultLib1->GetStatus(&status);
    //    assert(status == S_OK);
    //    pResultLib1->GetResult(&pResLib1);


    //    IDxcOperationResult* pResultLib2 = nullptr;
    //    IDxcBlob* pResLib2 = nullptr;

    //    m_pCompiler->Compile(sourceInclude.BlobEncoding, includeFile.c_str(), L"", L"lib_6_6", argsLink.data(), static_cast<uint32_t>(argsLink.size()), nullptr, 0, m_pIncludeHandler, &pResultLib2);

    //    pResultLib2->GetStatus(&status);
    //    assert(status == S_OK);
    //    pResultLib2->GetResult(&pResLib2);

    //    static IDxcLinker* pLinker = nullptr;

    //    LPCWSTR libName = L"entry";
    //    LPCWSTR libResName = L"res";
    //    if (pLinker == nullptr)
    //    {
    //        DxcCreateInstance(CLSID_DxcLinker, IID_PPV_ARGS(&pLinker));

    //        pLinker->RegisterLibrary(libName, pResLib1);

    //        pLinker->RegisterLibrary(libResName, pResLib2);
    //    }

    //    LPCWSTR              libNames[] = { libName, libResName };
    //    IDxcOperationResult* pLinkResult = nullptr;

    //    ShaderStageType gfxStage = pitem->m_ShageStage;
    //    if (gfxStage == ShaderStageType::Vertex)
    //    {
    //        status = pLinker->Link(L"vs_main", L"vs_6_6", libNames, 2, argsLink.data(), static_cast<uint32_t>(argsLink.size()), &pLinkResult);
    //        assert(status == S_OK);
    //        IDxcBlobEncoding* pErrors = nullptr;
    //        pLinkResult->GetErrorBuffer(&pErrors);
    //    }
    //    else
    //    {
    //        pLinker->Link(L"ps_main", L"ps_6_6", libNames, 2, argsLink.data(), static_cast<uint32_t>(argsLink.size()), &pLinkResult);
    //    }
    //    pLinkResult->GetStatus(&status);
    //    assert(status == S_OK);


    //    IDxcBlob* pProgramSourceRS = nullptr;
    //    pLinkResult->GetResult(&pProgramSourceRS);
    //    assert(pProgramSourceRS != nullptr);

    //    DxcBuffer bufLink = {};
    //    bufLink.Ptr = pProgramSourceRS->GetBufferPointer();
    //    bufLink.Size = pProgramSourceRS->GetBufferSize();
    //    bufLink.Encoding = CP_ACP;

    //    ID3D12ShaderReflection* pReflection = nullptr;
    //    hrRes = m_pUtils->CreateReflection(&bufLink, IID_PPV_ARGS(&pReflection));
    //    assert(hrRes == S_OK);

    //    *outRefl = pReflection;
    //    *outBlob = pProgramSourceRS;

    //    return true;
    //}

    bool KGFX_DXCComplierDX12::CreateBlobFromText(const char* pText, IDxcBlobEncoding** ppBlob) const
    {
        bool    bRet = false;
        HRESULT hrRes = E_FAIL;

        hrRes = m_pUtils->CreateBlobFromPinned(pText, static_cast<uint32_t>(strlen(pText) + 1), CP_UTF8, ppBlob);
        KGLOG_COM_PROCESS_ERROR(hrRes);

        bRet = true;
    Exit0:
        return bRet;
    }

    void KGFX_DXCComplierDX12::InitArg()
    {
#ifdef _DEBUG
        m_ComplierArgs.clear();

        m_ComplierArgs.push_back(disableOptimizations);
        m_ComplierArgs.push_back(enable16BITType);

        for (auto eachStr : debugInformation)
        {
            m_ComplierArgs.push_back(eachStr);
        }

        for (auto eachStr : hlslVersion)
        {
            m_ComplierArgs.push_back(eachStr);
        }

        for (auto eachStr : autoResBind)
        {
            m_ComplierArgs.push_back(eachStr);
        }
#else
        m_ComplierArgs.clear();

        m_ComplierArgs.push_back(enable16BITType);
        m_ComplierArgs.push_back(OptimizationLevel);

        for (auto eachStr : hlslVersion)
        {
            m_ComplierArgs.push_back(eachStr);
        }

        for (auto eachStr : autoResBind)
        {
            m_ComplierArgs.push_back(eachStr);
        }

#endif
    }

    void KGFX_DXCComplierDX12::SetIncludePath(const std::filesystem::path& includePath)
    {
        m_IncludePath = includePath;

        if (m_pIncludeHandler)
        {
            m_pIncludeHandler->SetIncludeRootPath(includePath);
        }
    }

    PersistentCache* KGFX_DXCComplierDX12::GetCacheManager() const
    {
        assert(m_pShaderCacheManager);
        return m_pShaderCacheManager;
    }

    bool KGFX_DXCComplierDX12::CheckShaderHasErrorMesg(const std::string& mesg) const
    {
        std::regex               errorRegex(R"((.*error:.*))");
        std::smatch              match;
        std::istringstream       iss;
        std::string              line;
        std::vector<std::string> errors;

        iss.str(mesg);
        while (std::getline(iss, line))
        {
            if (std::regex_search(line, match, errorRegex))
            {
                errors.push_back(line);
            }
        }

        return !errors.empty();
    }

    CComPtr<IDxcCompilerArgs> KGFX_DXCComplierDX12::CreateComplierArgs(const std::wstring& sourceName, const std::wstring& entryName, const std::wstring& shaderProfile, std::vector<LPCWSTR> compileArgs, const std::vector<DxcDefine>& compileDef) const
    {
        CComPtr<IDxcCompilerArgs> complArgs = nullptr;
        HRESULT hr = m_pUtils->BuildArguments(sourceName.c_str(), entryName.c_str(), shaderProfile.c_str(), compileArgs.data(), static_cast<uint32_t>(compileArgs.size()), compileDef.data(), static_cast<uint32_t>(compileDef.size()), &complArgs);
        KGLOG_COM_PROCESS_ERROR(hr);
        KGLOG_PROCESS_ERROR(complArgs);

    Exit0:
        return complArgs;
    }

    CComPtr<IDxcCompilerArgs> KGFX_DXCComplierDX12::CreatePreprocessArgs(const std::wstring& sourceName, const std::wstring& entryName, const std::vector<DxcDefine>& compileDef) const
    {
        CComPtr<IDxcCompilerArgs> complArgs = nullptr;
        std::vector<LPCWSTR>      compileArgs = {};
        compileArgs.emplace_back(L"-P");

        HRESULT hr = m_pUtils->BuildArguments(sourceName.c_str(), entryName.c_str(), nullptr, compileArgs.data(), static_cast<uint32_t>(compileArgs.size()), compileDef.data(), static_cast<uint32_t>(compileDef.size()), &complArgs);
        KGLOG_COM_PROCESS_ERROR(hr);
        KGLOG_PROCESS_ERROR(complArgs);

    Exit0:
        return complArgs;
    }

    std::vector<LPCWSTR> KGFX_DXCComplierDX12::CreateRewriteArgs(const std::wstring& entryName) const
    {
        std::vector<LPCWSTR> rewriteArgs = {};
        rewriteArgs.emplace_back(L"-E");
        rewriteArgs.emplace_back(entryName.c_str());
        //rewriteArgs.emplace_back(L"-remove-unused-globals");
        //rewriteArgs.emplace_back(L"-remove-unused-functions");
        return rewriteArgs;
    }

    std::vector<DxcDefine> KGFX_DXCComplierDX12::CreateBuildMarcoDefine(const std::unordered_map<std::string, std::string>& defineMarco, std::vector<std::pair<std::wstring, std::wstring>>& defineWMarco) const
    {
        defineWMarco = {};
        std::vector<DxcDefine> resMarco = {};

        for (const auto& macro : defineMarco)
        {
            std::wstring key = Utf8ToWString(macro.first);
            std::wstring value = {};

            if (!macro.second.empty())
            {
                value = Utf8ToWString(macro.second);
            }

            defineWMarco.emplace_back(key, value);
        }

        for (const auto& wmacro : defineWMarco)
        {
            DxcDefine dxcDefine = {};
            dxcDefine.Name = wmacro.first.c_str();
            dxcDefine.Value = wmacro.second.empty() ? nullptr : wmacro.second.c_str();
            resMarco.emplace_back(dxcDefine);
        }
        resMarco.emplace_back(dx12RHI);
        resMarco.emplace_back(SpecializationConstants);
        return resMarco;
    }

    bool KGFX_DXCComplierDX12::BuildDXReflection(IDxcBlob* pBlob, ID3D12ShaderReflection** outRefl) const
    {
        bool                    bRet = false;
        HRESULT                 hrRes = {};
        ID3D12ShaderReflection* pReflection = nullptr;
        DxcBuffer               buildRes = {};
        buildRes.Ptr = pBlob->GetBufferPointer();
        buildRes.Size = pBlob->GetBufferSize();
        buildRes.Encoding = CP_ACP;

        hrRes = m_pUtils->CreateReflection(&buildRes, IID_PPV_ARGS(&pReflection));
        KGLOG_COM_PROCESS_ERROR(hrRes);

        *outRefl = pReflection;

        bRet = true;
    Exit0:
        return bRet;
    }

    bool KGFX_DXCComplierDX12::BuildDXLibReflection(IDxcBlob* pBlob, ID3D12LibraryReflection** outRefl) const
    {
        bool                    bRet = false;
        HRESULT                 hrRes = {};
        ID3D12LibraryReflection* pReflection = nullptr;
        DxcBuffer               buildRes = {};
        buildRes.Ptr = pBlob->GetBufferPointer();
        buildRes.Size = pBlob->GetBufferSize();
        buildRes.Encoding = CP_ACP;

        hrRes = m_pUtils->CreateReflection(&buildRes, IID_PPV_ARGS(&pReflection));
        KGLOG_COM_PROCESS_ERROR(hrRes);

        *outRefl = pReflection;

        bRet = true;
    Exit0:
        return bRet;
    }

} // namespace gfx
