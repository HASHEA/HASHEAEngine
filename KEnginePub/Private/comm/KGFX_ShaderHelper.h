#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <mutex>
#include "KBase/Public/KBasePub.h"
#include "KEnginePub/Private/IKShaderreflector.h"

namespace IncludeFileHelper
{
    BOOL        _ReadFromTo(const char* pString, char cBegin, char cEnd, char* pOut);
    const char* _GetShaderTypeName(gfx::ShaderStageType eShaderStage);
    const char* GetShaderTypeName(gfx::ShaderStageType eShaderStage);
    BOOL        _IsCurCommentLine(const char* szline);
    BOOL        _IsDigitString(const char* p);
    int         _GetSMMaterialID(const char* s);
    int         _GetReflectionID(const char* s);
    BOOL        ReadWholeShaderFile(const NSKBase::tagFileLocation& sShaderLoc, std::string& szWholeFileString, std::vector<std::string>* pVecReadedFile);
    void        ExpandMacoDefineDXC(const char* szMacroDefine, std::unordered_map<std::string, std::string>& szOutString);

    BOOL   ReadUserShaderMtlId(NSKBase::tagFileLocation &sUserShaderFileLoc, int32_t &nMaterialID, int32_t& nReflectionID, char &uVaryingMask);
}; // namespace IncludeFileHelper

// 这些类只分别读取文件的主shader和子shader的字符串，不做任何其他的fix处理
namespace gfx
{
    class KGFX_DXCComplierDX12;
    class KGFX_ShaderFile;
    class KGFX_CombindShaderResult;

    // 对应@vs @fs 里面解析的具体主shader的内容
    class KGFX_ShaderFile : public KGfxRef
    {
    public:
        KGFX_ShaderFile();
        ~KGFX_ShaderFile();

        int32_t AddRef() override;
        int32_t GetRef() override;
        int32_t Release() override;

        BOOL LoadFile(const NSKBase::tagFileLocation& sShaderLoc, const char* szSectionName);

        bool OnlyLoadFile(const NSKBase::tagFileLocation& sShaderLoc);

        BOOL LoadFileFromWholeFileString(
            const std::string&              szWholeFileString,
            const NSKBase::tagFileLocation& sShaderLoc,
            const char*                     szSectionName
        );

        const char* GetFileContent();
        size_t      GetFileContentSize();
        const char* GetKey();
        void        SetKey(const char* pKey);

    private:
        NSKBase::tagFileLocation m_sShaderLoc;
        std::string              m_szSectionName;

        std::string              m_szContent;
        std::string              m_szKey;
        std::vector<std::string> m_vecReadedFileList;
        std::set<std::string>    m_setAddedInclude;
    };


    // pass item 对应的是某某@@pass @vs或 @fs等等，里面包含 KGFX_ShaderPassItemContent
    class KGFX_ShaderTechItem : public KGfxRef
    {
        friend class KGFX_ShaderFilePool;

    public:
        KGFX_ShaderTechItem();
        ~KGFX_ShaderTechItem();

        BOOL LoadFile(
            gfx::ShaderStageType eShaderStage,
            const char*          szShaderSource,
            const char*          szShaderDefPassName,
            const char*          szShaderTypeName,
            const char*          szUserShaderName
        );

        BOOL LoadFileFromWholeFileString(
            gfx::ShaderStageType            eShaderStage,
            const char*                     szShaderSource,
            const std::string&              szWholeFileString,
            const char*                     szShaderTechName,
            const char*                     szShaderTypeName,
            const NSKBase::tagFileLocation& sUserShaderLoc
        );

        BOOL LoadFileFromWholeFileStringNoFix(
            gfx::ShaderStageType            eShaderStage,
            const char*                     szShaderSource,
            const std::string&              szWholeFileString,
            const char*                     szShaderTechName,
            const char*                     szShaderTypeName,
            const NSKBase::tagFileLocation& sUserShaderLoc
        );

        /**
         * 只是读取shader文件的内容
         * @param szShaderName
         * @param pMacroDefine
         * @return
         */
        BOOL LoadFileFromWholeFileStringNoFix2(const char* szShaderName, const char* pMacroDefine);

        const char*          GetKey();
        void                 SetKey(const char* pKey);
        BOOL                 CombineShader(const char* szMacro, gfx::IKGFX_CombinedShaderResult* pCombineShaderResult);
        int32_t              AddRef() override;
        int32_t              GetRef() override;
        int32_t              Release() override;
        gfx::ShaderStageType GetShaderStage();
        const char*          GetEntryPoint();

        // gfx::enumShaderStageFlag m_eShageStage;
        gfx::ShaderStageType m_eShageStage         = {};
        std::string          m_szShaderSource      = {};
        std::string          m_szShaderDefPassName = {};
        std::string          m_szShaderTypeName    = {};
        std::string          m_szUserShaderSource  = {};

        std::string                                  m_szTechMacro     = {};
        // std::vector<std::pair<std::string, std::string>> m_szTechMacroDXC;
        std::unordered_map<std::string, std::string> m_szTechMacroDXC  = {};
        std::string                                  m_szEntryPoint    = {};
        std::string                                  m_szShaderName    = {};
        std::filesystem::path                        m_UserShaderPath  = {};
        KGFX_ShaderFile*                             m_pMainShaderFile = nullptr;

        /**
         * 这个需要删除，usershader不应该由我们来读
         */
        KGFX_ShaderFile* m_pUserShaderFile = nullptr;
        std::string      m_szKey;
    };

    class KGFX_ShaderFilePool
    {
        friend class KGFX_ShaderTechItem;

    public:
        KGFX_ShaderFilePool();
        ~KGFX_ShaderFilePool();

        // 通过shader文件直接加载
        KGFX_ShaderTechItem* RequestFromShaderFile(
            gfx::ShaderStageType            eShaderStage,
            const char*                     szShaderFileName,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char*                     szSection = nullptr
        );

        // 通过tech文件加载
        KGFX_ShaderTechItem* RequestFromTechFile(
            gfx::ShaderStageType            eShaderStage,
            const char*                     szTechFileName,
            const char*                     szTechName,
            const NSKBase::tagFileLocation& sUserShaderLoc
        );


        BOOL RemoveShaderFile(const char* pKey);
        BOOL RemoveTechItem(const char* pKey);


        KGFX_ShaderFile* RequestStaticShaderFile(
            gfx::ShaderStageType            eShaderStage,
            const NSKBase::tagFileLocation& sShaderLoc,
            const char*                     szSectionName
        );

        KGFX_ShaderFile* OnlyLoadShaderFile(
            const NSKBase::tagFileLocation& sShaderLoc,
            const char*                     szSectionName
        );


    private:
        std::mutex                                        m_ShaderMainShaderFileLock;
        std::unordered_map<std::string, KGFX_ShaderFile*> m_mapShaderFile;

        std::mutex                                            m_shaderTechItemLock;
        std::unordered_map<std::string, KGFX_ShaderTechItem*> m_mapShaderTechItem;
    };


    void                      KGFX_CreateShaderFilePool();
    void                      KGFX_DestroyShaderFilePool();
    gfx::KGFX_ShaderFilePool* KGFX_GetShaderFilePool();

    void                       KGFX_CreateDXCComplier();
    void                       KGFX_DestroyDXCComplier();
    gfx::KGFX_DXCComplierDX12* KGFX_GetDXCComplier();

}; // namespace gfx
