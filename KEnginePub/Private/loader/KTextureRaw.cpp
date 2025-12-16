#include "KTextureRaw.h"
#include "KBase/Public/io/KFile.h"
#include "KBase/Public/str/KStrHelper.h"
#include "Engine/KGLog.h"
#include "KTexturePool.h"
#include <cassert>
#include "KBase/Public/thread/KThread.h"
//////////////////////////////////////////////////////////////////////////
#include "KBase/Public/KMemLeak.h"

using namespace gfx;

KTextureRaw::KTextureRaw()
    : m_nRef(1)
    , m_nameHash(0)
    , m_pRawData(nullptr)
    , m_uWidth(0)
    , m_uHeight(0)
    , m_pMemTextureVK(nullptr)
    , m_uBytes(0)
    , m_pTerrainHLB(nullptr)
{
    SetIsProcedural(TRUE);
}

KTextureRaw::~KTextureRaw()
{
    SAFE_DELETE_ARRAY(m_pRawData);
    SAFE_RELEASE(m_pMemTextureVK);
    SAFE_DELETE(m_pTerrainHLB);
}


int KTextureRaw::AddRef()
{
    int n = ++m_nRef;
    return n;
}

int KTextureRaw::Release()
{
    KTexturePool* pTexturePool = (KTexturePool*)NSEngine::GetTexturePool();
    ASSERT(pTexturePool);
    if (this == (void*)pTexturePool->GetErrorTexture())
        return 1;
    else if (this == (void*)pTexturePool->GetErrorTextureCube())
        return 1;
    else if (this == (void*)pTexturePool->GetErrorTextureArray())
        return 1;

    int nRefCount = --m_nRef;
    ASSERT(nRefCount >= 0);
    if (nRefCount == 0)
    {
        pTexturePool->RemoveTexture(this);
    }
    return nRefCount;
}

int KTextureRaw::GetRef()
{
    return m_nRef;
}

KRESOURCETYPE KTextureRaw::GetResourceType()
{
    return TEXTURE_RAW_TYPE;
}

uint64_t gfx::KTextureRaw::GetResourceSize()
{
    // UNDONE KTextureRaw 【wait check】Byte
    return m_uBytes;
}

BOOL KTextureRaw::PostLoad()
{
    ASSERT(IsMainThread());

    BOOL bRet     = false;
    BOOL bRetCode = false;

    KG_PROCESS_ERROR(IsLoaded());
    KG_PROCESS_SUCCESS(IsPostLoaded());
    KGLOG_ASSERT_EXIT(m_pRawData);

    SAFE_RELEASE(m_pMemTextureVK);

    {
        auto* pTexturePool = NSEngine::GetTexturePool();

        bRetCode = pTexturePool->CreateMemTexture(&m_pMemTextureVK, m_uWidth, m_uHeight, enumTextureFormat::TEX_FORMAT_R8_UNORM, m_pRawData, m_uBytes);
        KGLOG_PROCESS_ERROR(bRetCode);
    }
Exit1:
    bRet = true;
Exit0:
    SAFE_DELETE_ARRAY(m_pRawData);
    SetLoadState(bRet ? ELoadableState::PostLoaded : ELoadableState::Failed);
    return bRet;
}

BOOL KTextureRaw::Load()
{
    BOOL    bResult = FALSE;
    KGFile* pFile   = nullptr;

    if (IsLoaded())
    {
        goto Exit1;
    }

    if (IsLoadFailed())
    {
        goto Exit0;
    }

    {
        // 现在暂时只处理正方形的R8 RAW格式
        pFile = KGFOpen(m_ustrName.Str(), "rb");

        long nLen     = 0;
        long nReadLen = 0;
        KGLOG_PROCESS_ERROR(pFile);

        KGFSeek(pFile, 0, SEEK_END);
        nLen = KGFTell(pFile);

        KGFSeek(pFile, 0, SEEK_SET);


        if (nLen != 513 * 513)
        {
            KGLogPrintf(KGLOG_ERR, "地形挖洞数据异常：%s : %dx%d=%d != %d", m_ustrName.Str(), 513, 513, 513 * 513, nLen);
            ASSERT(0);
            goto Exit0;
        }
        SAFE_DELETE_ARRAY(m_pRawData);

        m_pRawData = new uint8_t[nLen];
        KGLOG_PROCESS_ERROR(m_pRawData);

        nReadLen = KGFRead(m_pRawData, nLen, pFile);
        KGLOG_PROCESS_ERROR(nReadLen == nLen);

        m_uWidth = (uint32_t)sqrt((double)nReadLen);
        KGLOG_PROCESS_ERROR(m_uWidth * m_uWidth == nReadLen);

        m_uHeight = m_uWidth;
        m_uBytes  = (uint32_t)nLen;

        if (strstr(m_ustrName.Str(), ".hlb"))
        {
            ASSERT(m_uWidth == 513 && m_uHeight == 513);
            m_pTerrainHLB = new TerrainHLB(m_pRawData, m_uWidth, m_uHeight);
        }
    }
Exit1:
    bResult = TRUE;
Exit0:
    SetLoadState(bResult ? ELoadableState::Loaded : ELoadableState::Failed);
    if (pFile)
    {
        KGFClose(pFile);
    }
    return bResult;
}

uint32_t KTextureRaw::GetFormat()
{
    return (uint32_t)RenderTextureFormat::R8;
}

uint32_t KTextureRaw::GetWidth() const
{
    return m_uWidth;
}
uint32_t KTextureRaw::GetHeight() const
{
    return m_uHeight;
}

TextureType KTextureRaw::GetTextureType(void) const
{
    return TextureType::Texture2D;
}

void KTextureRaw::SetResourceName(const char* pcszFileName)
{
    m_nameHash = KSTR_HELPER::GetHashCodeForString64Bit(pcszFileName);
    m_ustrName = g_CachePathString(pcszFileName, TRUE);
}

KUniqueStr KTextureRaw::GetResourceName()
{
    return m_ustrName;
}

const char* KTextureRaw::GetName()
{
    return m_ustrName.Str();
}

uint64_t KTextureRaw::GetNameHash()
{
    return m_nameHash;
}

gfx::KGfxMemTexture* KTextureRaw::GetMemTextureVK()
{
    return m_pMemTextureVK;
}

void* KTextureRaw::GetNativeImageHandle() const
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

IKGFX_TextureResource* KTextureRaw::GetTextureResource() const
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

IKGFX_TextureView* KTextureRaw::GetSRV() const
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

uint64_t KTextureRaw::GetId()
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

const KGFX_TextureDesc& KTextureRaw::GetTexDesc() const
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

KGfxSubresourceRange KTextureRaw::ResolveSubresourceRange(const KGfxSubresourceRange& range)
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

TerrainHLB* KTextureRaw::GetTerrainHLB()
{
    return m_pTerrainHLB;
}

TerrainHLB::TerrainHLB(uint8_t* pRawData, uint32_t width, uint32_t height)
{
    memset(this, 0, sizeof(TerrainHLB));
    ASSERT(width == 513 && height == 513);
    for (uint32_t i = 0; i < 16; ++i)
    {
        for (uint32_t j = 0; j < 16; ++j)
        {
            // BOOL bPrint = false;
            // if (i == 15 && j == 15)
            //{
            //   bPrint = true;
            // }
            // if (bPrint)
            //   printf("block: %d %d \r\n", i, j);
            for (uint32_t ii = 0; ii < 33; ++ii)
            {
                for (uint32_t jj = 0; jj < 33; ++jj)
                {
                    uint32_t _i = i * 32 + ii;
                    uint32_t _j = j * 32 + jj;

                    ASSERT(_i < 513 && _j < 513);
                    uint32_t id = _i * 513 + _j;
                    if (pRawData[id] < 128)
                    {
                        m_block32[i][j]              = true;
                        uint32_t _b128_i             = i / 4;
                        uint32_t _b128_j             = j / 4;
                        m_block128[_b128_i][_b128_j] = true;
                    }
                    // if (bPrint)
                    //   printf("(%d,%d:%d)", ii, jj, id);
                }
                // if (bPrint)
                //   printf("\r\n");
            }
        }
    }
}

BOOL TerrainHLB::HasTerrainRegionHole(uint32_t i, uint32_t j)
{
    ASSERT(i < 16 && j < 16);
    return m_block32[15 - i][j];
}

BOOL TerrainHLB::HasTerrainSectionHole(uint32_t i, uint32_t j)
{
    ASSERT(i < 4 && j < 4);
    return m_block128[3 - i][j];
}
