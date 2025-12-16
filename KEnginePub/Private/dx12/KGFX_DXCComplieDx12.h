#pragma once
#include <Windows.h>
#include <dxc_2025_02_20/inc/dxcapi.h>
#include <dxc_2025_02_20/inc/dxctools.h>
#include <atlcomcli.h>
#include "KEnginePub/Public/IGFX_Public.h"
#include <d3dcompiler.h>
#include <filesystem>
#include "KGFX_ShaderCache.h"

namespace gfx
{
    class KGFX_ShaderTechItemDX12;
}

namespace gfx
{
    /// <summary>
    /// Provides a QueryInterface implementation for a class that supports
    /// any number of interfaces in addition to IUnknown.
    /// </summary>
    /// <remarks>
    /// This implementation will also report the instance as not supporting
    /// marshaling. This will help catch marshaling problems early or avoid
    /// them altogether.
    /// </remarks>
    template <typename TObject>
    HRESULT DoBasicQueryInterface_recurse(TObject* self, REFIID iid, void** ppvObject)
    {
        return E_NOINTERFACE;
    }
    template <typename TObject, typename TInterface, typename... Ts>
    HRESULT DoBasicQueryInterface_recurse(TObject* self, REFIID iid, void** ppvObject)
    {
        if (ppvObject == nullptr)
            return E_POINTER;
        if (IsEqualIID(iid, __uuidof(TInterface)))
        {
            *(TInterface**)ppvObject = self;
            self->AddRef();
            return S_OK;
        }
        return DoBasicQueryInterface_recurse<TObject, Ts...>(self, iid, ppvObject);
    }
    template <typename... Ts, typename TObject>
    HRESULT DoBasicQueryInterface(TObject* self, REFIID iid, void** ppvObject)
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        // Support INoMarshal to void GIT shenanigans.
        if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(INoMarshal)))
        {
            *ppvObject = reinterpret_cast<IUnknown*>(self);
            reinterpret_cast<IUnknown*>(self)->AddRef();
            return S_OK;
        }

        return DoBasicQueryInterface_recurse<TObject, Ts...>(self, iid, ppvObject);
    }

    template<typename T>
    struct ComReleaser
    {
        void operator()(T* p) const
        {
            if (p) p->Release();
        }
    };

    std::string WCharToUtf8(LPCWSTR lpwstr);


    class SimpleIncludeHanlder : public IDxcIncludeHandler
    {
    public:
        SimpleIncludeHanlder(IDxcLibrary* pLibrary);

        virtual ~SimpleIncludeHanlder();

        ULONG __stdcall AddRef() noexcept override;

        ULONG __stdcall Release() override;

        HRESULT QueryInterface(const IID& riid, void** ppvObject) override;

        HRESULT LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override;

        void SetUserShaderName(const std::filesystem::path& path);

        void SetIncludeRootPath(const std::filesystem::path& path);

        NSKBase::tagFileLocation GetPackUserShaderLocation() const;
    private:
        std::filesystem::path m_UserShaderPath = {};
        IDxcLibrary* m_pLibrary = nullptr;
        volatile std::atomic<int32_t> m_dwRef = { 1 };
        std::filesystem::path m_ShaderIncludeRootPath = {};
        const std::filesystem::path defaultUserShaderName = L"UserShader.fx5";
        const std::filesystem::path defaultUserShaderPath = L"enginedata/material/shader_dx12/include/UserShader.fx5";
    };



    class KGFX_DXCComplierDX12
    {
    public:
        KGFX_DXCComplierDX12() = default;
        ~KGFX_DXCComplierDX12();

        KGFX_DXCComplierDX12(const KGFX_DXCComplierDX12&) = delete;
        KGFX_DXCComplierDX12& operator=(const KGFX_DXCComplierDX12&) = delete;

        bool Init();

        void Reset();

        /**
         * 预处理并展开shader的文本，用于计算shader的变化来确定是否需要重新编译
         * @param pitem
         * @param outShader
         * @param outErrorMsg
         * @return
         */
        bool PreprocessShader(const KGFX_ShaderTechItemDX12* pitem, std::string& outShader, std::string& outErrorMsg) const;

        /**
         * 与PreprocessShader一样，区别是会将展开shader中不会使用的函数和声明文本去掉
         * @param pitem
         * @param outShader
         * @param outErrorMsg
         * @return
         */
        bool PreprocessShaderRemoveUnused(KGFX_ShaderTechItemDX12* pitem, std::string& outShader, std::string& outErrorMsg) const;

        /**
         * 对shader进行全文本展开并检查缓存是否正确，如果有缓存就读取缓存，无缓存则编译并存储缓存信息
         * @param pitem
         * @param outRefl
         * @param outBlob
         * @param outErrorMsg
         * @return
         */
        bool CheckShaderCache(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        /**
         * 跟上面函数功能相同，这个是给编译成lib用的
         * @param pitem
         * @param outRefl
         * @param outBlob
         * @param outErrorMsg
         * @return
         */
        bool CheckLibShaderCache(KGFX_ShaderTechItemDX12* pitem, ID3D12LibraryReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        /**
         * 使用DXC编译shader并获取反射信息
         * @param pitem
         * @param outRefl
         * @param outBlob
         * @param outErrorMsg
         * @return
         */
        bool CompileShader(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg);

        /**
         * 这个和CompileShader实现的功能是一样的，但是DXC建议使用新的接口
         * @param pitem
         * @param outRefl
         * @param outBlob
         * @param outErrorMsg
         * @return
         */
        bool CompileShader3(KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        /**
         * 这个和CompileShader3实现的功能是一样的，只不过使用的shader文本是把include和宏全展开之后的文本
         * @param shaderSource
         * @param pitem
         * @param outRefl
         * @param outBlob
         * @param outErrorMsg
         * @return
         */
        bool CompileShaderFullText3(const DxcBuffer& shaderSource, const KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        /**
        * 跟上面函数功能相同，这个是给编译成lib用的
        * @param shaderSource
        * @param pitem
        * @param outRefl
        * @param outBlob
        * @param outErrorMsg
        * @return
        */
        bool CompileLibShaderFullText3(const DxcBuffer& shaderSource, const KGFX_ShaderTechItemDX12* pitem, ID3D12LibraryReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        //bool CompileShaderLink(const KGFX_ShaderTechItemDX12* pitem, ID3D12ShaderReflection** outRefl, IDxcBlob** outBlob, std::string& outErrorMsg) const;

        bool CreateBlobFromText(const char* pText, IDxcBlobEncoding** ppBlob) const;

        void SetProfileVersion(ProfileVersion version)
        {
            m_ProfileVersion = version;
        }

        ProfileVersion GetProfileVersion() const
        {
            return m_ProfileVersion;
        }

        void InitArg();

        void SetIncludePath(const std::filesystem::path& includePath);

        PersistentCache* GetCacheManager() const;

        bool CheckShaderHasErrorMesg(const std::string& mesg) const;

    private:
        CComPtr<IDxcCompilerArgs> CreateComplierArgs(const std::wstring& sourceName, const std::wstring& entryName, const std::wstring& shaderProfile, std::vector<LPCWSTR> compileArgs, const std::vector<DxcDefine>& compileDef) const;

        CComPtr<IDxcCompilerArgs> CreatePreprocessArgs(const std::wstring& sourceName, const std::wstring& entryName, const std::vector<DxcDefine>& compileDef) const;

        std::vector<LPCWSTR> CreateRewriteArgs(const std::wstring& entryName) const;

        std::vector<DxcDefine> CreateBuildMarcoDefine(const std::unordered_map<std::string, std::string>& defineMarco, std::vector<std::pair<std::wstring, std::wstring>>& defineWMarco) const;

        bool BuildDXReflection(IDxcBlob* pBlob, ID3D12ShaderReflection** outRefl) const;
        bool BuildDXLibReflection(IDxcBlob* pBlob, ID3D12LibraryReflection** outRefl) const;

        CComPtr<IDxcCompiler> m_pCompiler = nullptr;
        CComPtr<IDxcCompiler3> m_pCompiler3 = nullptr;
        CComPtr<IDxcLibrary> m_pLibrary = nullptr;
        CComPtr<IDxcUtils> m_pUtils = nullptr;
        ProfileVersion m_ProfileVersion = ProfileVersion::_6_6;

        CComPtr<IDxcRewriter> m_pRewriter = nullptr;
        CComPtr<IDxcRewriter2> m_pRewriter2 = nullptr;

        CComPtr<IDxcVersionInfo> m_pDxcVersionInfo = nullptr;
        CComPtr<IDxcVersionInfo3> m_pDxcVersionInfo3 = nullptr;
        std::string m_DxcVersion = {};
        /**
         * 所有编译需要的选项和参数
         */
        std::vector<LPCWSTR> m_ComplierArgs = { };

        /**
         * 处理编译时include路径的文件读取
         */
        SimpleIncludeHanlder* m_pIncludeHandler = nullptr;

        RefPtr<PersistentCache> m_pShaderCacheManager = {};

        std::filesystem::path m_IncludePath = {};

        static constexpr bool m_bUseRewrite = true;

        static constexpr LPCWSTR hlslVersion[] = { L"-HV",L"2021" };
        static constexpr LPCWSTR debugInformation[] = { L"-Zi",L"-Qembed_debug" };
        static constexpr LPCWSTR enable16BITType = { L"-enable-16bit-types" };
        static constexpr LPCWSTR disableOptimizations = { L"-Od" };
        static constexpr LPCWSTR OptimizationLevel = { L"-O3" };
        static constexpr LPCWSTR autoResBind[] = { L"-auto-binding-space",L"0" };
        static constexpr DxcDefine dx12RHI = { L"__RHI_API",L"__RHI_DX12" };
        static constexpr DxcDefine SpecializationConstants = { L"SHADER_MACRO_TO_SPECIALIZATION_CONSTANTS_ENABLE" };
    };
}

