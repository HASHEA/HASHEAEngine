#include "KTextureMask.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/str/KStrHelper.h"
#include "Engine/KGLog.h"
#include "KTexturePool.h"
#include <assert.h>
#include "KBase/Public/thread/KThread.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

using namespace gfx;

KTextureMask::KTextureMask()
    : m_nRef(1)
    , m_nameHash(0)
    , m_pRawData(nullptr)
    , m_uWidth(0)
    , m_uHeight(0)
    , m_pMemTextureVK(nullptr)
    , m_textureFormat(TEX_FORMAT_NONE)
{
    SetIsProcedural(TRUE);
}

KTextureMask::~KTextureMask()
{
    SAFE_DELETE_ARRAY(m_pRawData);
    SAFE_RELEASE(m_pMemTextureVK);
}


int KTextureMask::AddRef()
{
    int n = ++m_nRef;
    return n;
}

int KTextureMask::Release()
{
    KTexturePool* pTexturePool = (KTexturePool*)NSEngine::GetTexturePool();
    assert(pTexturePool);
    if (this == (void*)pTexturePool->GetErrorTexture())
        return 1;
    else if (this == (void*)pTexturePool->GetErrorTextureCube())
        return 1;
    else if (this == (void*)pTexturePool->GetErrorTextureArray())
        return 1;

    int nRefCount = --m_nRef;
    assert(nRefCount >= 0);
    if (nRefCount == 0)
    {
        pTexturePool->RemoveTexture(this);
    }
    return nRefCount;
}

int KTextureMask::GetRef()
{
    return m_nRef;
}


KRESOURCETYPE KTextureMask::GetResourceType()
{
    return TEXTURE_MASK_TYPE;
}

uint64_t gfx::KTextureMask::GetResourceSize()
{
    // UNDONE KTextureMask 创建的是KGFX_MemTexture
    return 0;
}

BOOL KTextureMask::Load()
{
    SetPostLoaded();
    return true;
}

BOOL KTextureMask::PostLoad()
{
    return true;
}

uint32_t KTextureMask::GetFormat()
{
    return (uint32_t)RenderTextureFormat::R8;
}

uint32_t KTextureMask::GetWidth() const
{
    return m_uWidth;
}
uint32_t KTextureMask::GetHeight() const
{
    return m_uHeight;
}

TextureType KTextureMask::GetTextureType(void) const
{
    return TextureType::Texture2D;
}

void KTextureMask::SetResourceName(const char* pcszFileName)
{
    m_nameHash = KSTR_HELPER::GetHashCodeForString64Bit(pcszFileName);
    m_ustrName = g_CachePathString(pcszFileName, TRUE);
}

KUniqueStr KTextureMask::GetResourceName()
{
    return m_ustrName;
}

const char* KTextureMask::GetName()
{
    return m_ustrName.Str();
}

uint64_t KTextureMask::GetNameHash()
{
    return m_nameHash;
}

gfx::KGfxMemTexture* KTextureMask::GetMemTextureVK()
{
    return m_pMemTextureVK;
}

void* KTextureMask::GetNativeImageHandle() const
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->GetNativeImageHandle();
    }
    else
    {
        return nullptr;
    }
}

uint64_t KTextureMask::GetId()
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->GetId();
    }
    else
    {
        return m_uTextureId;
    }
}

IKGFX_TextureResource* KTextureMask::GetTextureResource() const
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->GetTextureResource();
    }
    else
    {
        return nullptr;
    }
}

IKGFX_TextureView* KTextureMask::GetSRV() const
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->GetSRV();
    }
    else
    {
        return nullptr;
    }
}

const KGFX_TextureDesc& KTextureMask::GetTexDesc() const
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->GetTexDesc();
    }
    else
    {
        return KGFX_TextureDesc::g_EmptryValue;
    }
}

KGfxSubresourceRange KTextureMask::ResolveSubresourceRange(const KGfxSubresourceRange& range)
{
    if (m_pMemTextureVK)
    {
        return m_pMemTextureVK->ResolveSubresourceRange(range);
    }
    else
    {
        return KGfxSubresourceRange{};
    }
}

BOOL KTextureMask::Create(gfx::enumTextureFormat textureFormat, uint32_t width, uint32_t height, uint8_t* pData, uint32_t ubytes)
{
    BOOL bRet       = false;
    BOOL bRetCode   = false;
    m_textureFormat = textureFormat;
    m_uWidth        = width;
    m_uHeight       = height;

    bRetCode = FillData(pData, ubytes);
    KGLOG_PROCESS_ERROR(bRetCode);

    SetPostLoaded();
    bRet = true;
Exit0:
    return bRet;
}

BOOL KTextureMask::FillData(uint8_t* pData, uint32_t ubytes)
{
    BOOL bRet     = false;
    BOOL bRetCode = false;

    auto* pTexturePool = NSEngine::GetTexturePool();
    if (pTexturePool)
    {
        if (!m_pMemTextureVK)
        {
            bRetCode = pTexturePool->CreateMemTexture(&m_pMemTextureVK, m_uWidth, m_uHeight, m_textureFormat, pData, ubytes);
            KGLOG_PROCESS_ERROR(bRetCode);
        }
        else
        {
            bRetCode = m_pMemTextureVK->Update(pData, ubytes);
            KGLOG_PROCESS_ERROR(bRetCode);
        }

        SetPostLoaded();
    }
    else
    {
        KGLogPrintf(KGLOG_ERR, "KTexturePool is null");
    }
    bRet = true;
Exit0:
    return bRet;
}

BOOL KTextureMask::UpdateSubImage(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height, uint8_t* pData, uint32_t ubytes)
{
    BOOL bRet     = false;
    BOOL bRetCode = false;

    bRetCode = m_pMemTextureVK->UpdateSubImage(xoffset, yoffset, width, height, pData, ubytes);
    KGLOG_PROCESS_ERROR(m_pMemTextureVK);

    SetPostLoaded();
    bRet = true;
Exit0:
    return bRet;
}

BOOL KTextureMask::LoadFromRGBA8Data(unsigned int uWidth, unsigned int uHeight, const void* pData)
{
    BOOL bResult  = FALSE;
    BOOL bRetCode = FALSE;
    bRetCode      = Create(gfx::enumTextureFormat::TEX_FORMAT_R8G8B8A8_UNORM, uWidth, uHeight, (uint8_t*)pData, uWidth * uHeight * 4);
    KGLOG_PROCESS_ERROR(bRetCode);
    bResult = TRUE;
Exit0:
    SetLoadState(bResult ? ELoadableState::PostLoaded : ELoadableState::Failed);
    return bResult;
}
