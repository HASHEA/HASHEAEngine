#ifndef KTEXTURE_POOL_H
#define KTEXTURE_POOL_H

#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include "KEnginePub/Public/IKTexture.h"
#include "KEnginePub/Private/IKShader.h"
#include "KBase/Public/async_task/KAsyncTask.h"
#include "Engine/File.h"
#include "Engine/KUniqueString.h"
#include "KBase/Public/io/KFile.h"

#if defined(_WIN32) || defined(__MACOS__) || (defined(__linux__) && !defined(__ANDROID__))
#define DDS_HARDWARE 1
#endif

struct IKG_Buffer;

namespace gli
{
    class texture;
}

class KTexturePool : public IKTexturePool
{
    std::unordered_map<KUniqueStr, IKTexture*> m_mapTexture;
    std::mutex                                 m_mtxTexture;

    KResourceGC* m_pResoureceGC = nullptr;
    IKTexture*   m_pEvnMap      = nullptr;

    BOOL m_bHighLoadPriority;
    // KTextureLoadThread m_TextureLoadThread;

public:
    KTexturePool();
    virtual ~KTexturePool();

    // for performance

public:
    int  GetTextureSize();
    void TakeResourceSnap(IFile* pFile);

public:
    // memory texture
    BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, const uint32_t cuWidth, const uint32_t cuHeight, gfx::enumTextureFormat eTargetFormat, const void* cpInitBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;
    BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, const uint32_t cuWidth, const uint32_t cuHeight, const uint32_t cuDepth, gfx::enumTextureFormat eTargetFormat, const void* cpInitBytes = nullptr, uint32_t uBytes = 0, BOOL bTile = TRUE, BOOL bSupportSample = TRUE, BOOL bSupportStorage = FALSE) override;
    BOOL CreateMemTexture(gfx::KGfxMemTexture** ppRetMemoryTex, uint32_t uWidth, uint32_t uHeight, gfx::enumTextureFormat targetFormat, gfx::KRenderTarget* pSrcRT) override;

    // file texture
    IKTexture* RequestTexture(
        const KUniqueStr& ustrPackName, const KUniqueStr& ustrSubTexFileName,
        TextureType eTextureType = TextureType::Texture2D, BOOL bThreadLoad = FALSE,
        uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false
    );
    IKTexture* RequestTexture(const char* pcszResPath, TextureType eTextureType = TextureType::Texture2D, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false) override;
    IKTexture* RequestRawTexture(const char* pcszResName, uint32_t dwOption = 0);
    IKTexture* RequestEmptyTexture() override;
    IKTexture* RequestEmptyFileTexture() override;

    IKTexture* RequestMergedTexture2DArray(const char* pcszPathNames[], uint32_t uPathNameCount, TextureType eTextureType = TextureType::MergedTexture2DArray, uint32_t dwOption = 0, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false) override;

    IKTexture* GetTextureByResPath(const char* pcszResPath) override;
    IKTexture* GetEvnMap() override;
    void       FrameMove(float fDeltaTime) override;

    gfx::KGfxFileTexture* GetErrorTexture() override;
    gfx::KGfxFileTexture* GetErrorTextureArray() override;
    gfx::KGfxFileTexture* GetErrorTextureCube() override;
    gfx::KGfxFileTexture* GetBlankTexture() override;
    gfx::KGfxFileTexture* GetBlackTexture() override;
    gfx::KGfxFileTexture* GetDefaultNormalTexture() override;
    gfx::KRenderTarget* GetDefaultShadowTex() override;

    gfx::KTextureMask* CreateTextureMask() override;

    void SetToHighLoadPriority(BOOL bHigh) override;

    static BOOL TextureFileExist(const char* pcszResName);
    static BOOL TextureFileExist(KGIndexPkgFile* pIndexPkgFile, const char* pcszResName);
    static BOOL TextureFileExist(const NSKBase::tagFileLocation& sFileLoc);
    static BOOL TextureResourceAdjust(KGIndexPkgFile* pIndexPkgFile, const char* pcszResName, char(&szResult)[MAX_PATH]);
    static BOOL TextureResourceAdjust(const NSKBase::tagFileLocation& sFileLoc);

public:
    // 用于内部资源回收
    BOOL RemoveTexture(IKTexture* pTexture);

private:
    IKTexture* _GetTextureByResPath(KUniqueStr ustrResPath, bool bNeedLock = true);

    IKTexture* _RequestTextureImp(const char* szFileNames[], uint32_t uPathNameCount, TextureType eTextureType, uint32_t dwOption, uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false);
    IKTexture* _RequestTextureImp(
        const KUniqueStr& ustrPackName, const KUniqueStr& ustrSubTexFileName,
        TextureType eTextureType, BOOL bThreadLoad,
        uint32_t uOwnerModelFlags = 0, BOOL bHighLoadPriority = false, BOOL bForceTextureArray = false
    );

private:
    gfx::KGfxFileTexture* m_pBlankTexture = nullptr;
    gfx::KGfxFileTexture* m_pBlackTexture = nullptr;
    gfx::KGfxFileTexture* m_pErrorTexture = nullptr;
    gfx::KGfxFileTexture* m_pDefaultNormalTexture = nullptr;
    gfx::KGfxFileTexture* m_pErrorTextureArray = nullptr;
    gfx::KGfxFileTexture* m_pErrorTextureCube = nullptr;

    gfx::KRenderTarget* m_pDefaultShadowRenderTexture = nullptr;
};

#endif
