#include "KGFX_ShaderCache.h"
#include <KBase/Public/io/KFile.h>
#include <algorithm>
#include "KGFX_GraphiceDeviceDX12.h"
#include "KBase/Public/KMemLeak.h"

namespace gfx
{
    static constexpr int kShaderFilenameDigestBytes = 10;

    std::string DigestUtil::DigestToString(const void* digest, int digestSize)
    {
        assert(digest && digestSize >= 0);
        static const char* hex = "0123456789abcdef";

        const uint8_t* data = static_cast<const uint8_t*>(digest);

        // 真实可用的字节数（截断）
        int useBytes = digestSize;
        useBytes = std::min(useBytes, kShaderFilenameDigestBytes);

        std::string str;
        str.resize(useBytes * 2);

        for (int i = 0; i < useBytes; ++i)
        {
            unsigned b = data[i];
            str[2 * i] = hex[b >> 4];
            str[2 * i + 1] = hex[b & 0xF];
        }
        return str;
    }

    void* ShaderCacheFileImpl::OpenFile(const char* pcszFilePath) const
    {
        IFile* piFile = nullptr;

        KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
        piFile = g_OpenFile(pcszFilePath);

    Exit0:
        return piFile;
    }

    void* ShaderCacheFileImpl::OpenFileV5File(const char* pcszFilePath) const
    {
        IFile* piFile = nullptr;

        KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
        piFile = g_OpenFileInPak(pcszFilePath);

    Exit0:
        return piFile;
    }

    void* ShaderCacheFileImpl::OpenAloneFile(const char* pcszFilePath, BOOL bWritable) const
    {
        IFile* piFile = nullptr;

        KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
        if (bWritable)
        {
            piFile = g_CreateAloneFile(pcszFilePath);
        }
        else
        {
            piFile = g_OpenAloneFile(pcszFilePath, bWritable);
        }

    Exit0:
        return piFile;
    }

    bool ShaderCacheFileImpl::CloseFile(void* hFile) const
    {
        IFile* piFile = static_cast<IFile*>(hFile);
        if (!piFile)
            return false;

        piFile->Release();
        return true;
    }

    unsigned int ShaderCacheFileImpl::Read(void* hFile, void* pBuffer, unsigned int uReadBytes) const
    {
        unsigned int uResult = 0;
        IFile* piFile = nullptr;

        KGLOG_PROCESS_ERROR(hFile && pBuffer && uReadBytes > 0);

        piFile = static_cast<IFile*>(hFile);
        uResult = piFile->Read(pBuffer, uReadBytes);
    Exit0:
        return uResult;
    }

    unsigned int ShaderCacheFileImpl::Write(void* hFile, const void* pBuffer, unsigned int uWriteBytes) const
    {
        unsigned int uResult = 0;
        IFile* piFile = nullptr;

        KGLOG_PROCESS_ERROR(hFile && pBuffer && uWriteBytes > 0);

        piFile = static_cast<IFile*>(hFile);
        uResult = piFile->Write(pBuffer, uWriteBytes);
    Exit0:
        return uResult;
    }

    int ShaderCacheFileImpl::Seek(void* hFile, int Offset, int Origin) const
    {
        IFile* piFile = static_cast<IFile*>(hFile);
        if (!piFile)
            return false;

        return piFile->Seek(Offset, Origin);
    }

    bool ShaderCacheFileImpl::CacheFileExit(const char* pcszFilePath) const
    {
        bool bEXIT = false;
        KGLOG_PROCESS_ERROR(pcszFilePath && pcszFilePath[0]);
        bEXIT = KGFExist(pcszFilePath);

        return bEXIT;
    Exit0:
        return false;
    }

    int DigestUtil::GetHexDigitValue(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    bool DigestUtil::StringToDigest(const char* str, int strLength, void* digest, int digestSize)
    {
        assert(str && strLength >= 0 && digest && digestSize >= 0);

        if (strLength != digestSize * 2)
        {
            ::memset(digest, 0, digestSize);
            return false;
        }

        uint8_t* data = static_cast<uint8_t*>(digest);
        for (int i = 0; i < digestSize; ++i)
        {
            int upper = GetHexDigitValue(str[i * 2]);
            int lower = GetHexDigitValue(str[i * 2 + 1]);
            if (upper == -1 || lower == -1)
            {
                ::memset(digest, 0, digestSize);
                return false;
            }
            data[i] = static_cast<uint8_t>(lower | upper << 4);
        }

        return true;
    }

#pragma region SHA1
    gfx::SHA1::SHA1()
    {
        Init();
    }

    void gfx::SHA1::Init()
    {
        m_index = 0;
        m_bits = 0;
        m_State[0] = 0x67452301;
        m_State[1] = 0xefcdab89;
        m_State[2] = 0x98badcfe;
        m_State[3] = 0x10325476;
        m_State[4] = 0xc3d2e1f0;
    }

    void gfx::SHA1::Update(const void* data, size_t size)
    {
        if (!data || size <= 0)
        {
            return;
        }

        const uint8_t* ptr = static_cast<const uint8_t*>(data);

        // Fill up buffer if not full.
        while (size > 0 && m_index != 0)
        {
            AddByte(*ptr++);
            m_bits += 8;
            size--;
        }

        // Process full blocks.
        while (size >= sizeof(m_buf))
        {
            ProcessBlock(ptr);
            ptr += sizeof(m_buf);
            size -= sizeof(m_buf);
            m_bits += sizeof(m_buf) * 8;
        }

        // Process remaining bytes.
        while (size > 0)
        {
            AddByte(*ptr++);
            m_bits += 8;
            size--;
        }
    }

    gfx::SHA1::Digest gfx::SHA1::Finalize()
    {
        // Finalize with 0x80, some zero padding and the length in bits.
        AddByte(0x80);
        while (m_index % 64 != 56)
        {
            AddByte(0);
        }
        for (int i = 7; i >= 0; --i)
        {
            AddByte(static_cast<uint8_t>(m_bits >> i * 8));
        }

        Digest   digest;
        uint8_t* data = reinterpret_cast<uint8_t*>(digest.data);
        for (int i = 0; i < 5; i++)
        {
            for (int j = 3; j >= 0; j--)
            {
                data[i * 4 + j] = (m_State[i] >> ((3 - j) * 8)) & 0xff;
            }
        }

        return digest;
    }

    gfx::SHA1::Digest gfx::SHA1::Compute(const void* data, int size)
    {
        SHA1 sha1;
        sha1.Update(data, size);
        return sha1.Finalize();
    }

    void gfx::SHA1::AddByte(uint8_t x)
    {
        m_buf[m_index++] = x;

        if (m_index >= sizeof(m_buf))
        {
            m_index = 0;
            ProcessBlock(m_buf);
        }
    }

    void gfx::SHA1::ProcessBlock(const uint8_t* ptr)
    {
        auto rol32 = [](uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); };

        auto makeWord = [](const uint8_t* p) {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) |
                static_cast<uint32_t>(p[3]);
            };

        constexpr uint32_t c0 = 0x5a827999;
        constexpr uint32_t c1 = 0x6ed9eba1;
        constexpr uint32_t c2 = 0x8f1bbcdc;
        constexpr uint32_t c3 = 0xca62c1d6;

        uint32_t a = m_State[0];
        uint32_t b = m_State[1];
        uint32_t c = m_State[2];
        uint32_t d = m_State[3];
        uint32_t e = m_State[4];

        uint32_t w[16];

        for (size_t i = 0; i < 16; i++)
        {
            w[i] = makeWord(ptr + i * 4);
        }

#define SHA1_LOAD(i) \
    w[i & 15] = rol32(w[(i + 13) & 15] ^ w[(i + 8) & 15] ^ w[(i + 2) & 15] ^ w[i & 15], 1);
#define SHA1_ROUND_0(v, u, x, y, z, i)                       \
    z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + rol32(v, 5); \
    u  = rol32(u, 30);
#define SHA1_ROUND_1(v, u, x, y, z, i)                       \
    SHA1_LOAD(i)                                             \
    z += ((u & (x ^ y)) ^ y) + w[i & 15] + c0 + rol32(v, 5); \
    u  = rol32(u, 30);
#define SHA1_ROUND_2(v, u, x, y, z, i)               \
    SHA1_LOAD(i)                                     \
    z += (u ^ x ^ y) + w[i & 15] + c1 + rol32(v, 5); \
    u  = rol32(u, 30);
#define SHA1_ROUND_3(v, u, x, y, z, i)                             \
    SHA1_LOAD(i)                                                   \
    z += (((u | x) & y) | (u & x)) + w[i & 15] + c2 + rol32(v, 5); \
    u  = rol32(u, 30);
#define SHA1_ROUND_4(v, u, x, y, z, i)               \
    SHA1_LOAD(i)                                     \
    z += (u ^ x ^ y) + w[i & 15] + c3 + rol32(v, 5); \
    u  = rol32(u, 30);

        SHA1_ROUND_0(a, b, c, d, e, 0);
        SHA1_ROUND_0(e, a, b, c, d, 1);
        SHA1_ROUND_0(d, e, a, b, c, 2);
        SHA1_ROUND_0(c, d, e, a, b, 3);
        SHA1_ROUND_0(b, c, d, e, a, 4);
        SHA1_ROUND_0(a, b, c, d, e, 5);
        SHA1_ROUND_0(e, a, b, c, d, 6);
        SHA1_ROUND_0(d, e, a, b, c, 7);
        SHA1_ROUND_0(c, d, e, a, b, 8);
        SHA1_ROUND_0(b, c, d, e, a, 9);
        SHA1_ROUND_0(a, b, c, d, e, 10);
        SHA1_ROUND_0(e, a, b, c, d, 11);
        SHA1_ROUND_0(d, e, a, b, c, 12);
        SHA1_ROUND_0(c, d, e, a, b, 13);
        SHA1_ROUND_0(b, c, d, e, a, 14);
        SHA1_ROUND_0(a, b, c, d, e, 15);
        SHA1_ROUND_1(e, a, b, c, d, 16);
        SHA1_ROUND_1(d, e, a, b, c, 17);
        SHA1_ROUND_1(c, d, e, a, b, 18);
        SHA1_ROUND_1(b, c, d, e, a, 19);
        SHA1_ROUND_2(a, b, c, d, e, 20);
        SHA1_ROUND_2(e, a, b, c, d, 21);
        SHA1_ROUND_2(d, e, a, b, c, 22);
        SHA1_ROUND_2(c, d, e, a, b, 23);
        SHA1_ROUND_2(b, c, d, e, a, 24);
        SHA1_ROUND_2(a, b, c, d, e, 25);
        SHA1_ROUND_2(e, a, b, c, d, 26);
        SHA1_ROUND_2(d, e, a, b, c, 27);
        SHA1_ROUND_2(c, d, e, a, b, 28);
        SHA1_ROUND_2(b, c, d, e, a, 29);
        SHA1_ROUND_2(a, b, c, d, e, 30);
        SHA1_ROUND_2(e, a, b, c, d, 31);
        SHA1_ROUND_2(d, e, a, b, c, 32);
        SHA1_ROUND_2(c, d, e, a, b, 33);
        SHA1_ROUND_2(b, c, d, e, a, 34);
        SHA1_ROUND_2(a, b, c, d, e, 35);
        SHA1_ROUND_2(e, a, b, c, d, 36);
        SHA1_ROUND_2(d, e, a, b, c, 37);
        SHA1_ROUND_2(c, d, e, a, b, 38);
        SHA1_ROUND_2(b, c, d, e, a, 39);
        SHA1_ROUND_3(a, b, c, d, e, 40);
        SHA1_ROUND_3(e, a, b, c, d, 41);
        SHA1_ROUND_3(d, e, a, b, c, 42);
        SHA1_ROUND_3(c, d, e, a, b, 43);
        SHA1_ROUND_3(b, c, d, e, a, 44);
        SHA1_ROUND_3(a, b, c, d, e, 45);
        SHA1_ROUND_3(e, a, b, c, d, 46);
        SHA1_ROUND_3(d, e, a, b, c, 47);
        SHA1_ROUND_3(c, d, e, a, b, 48);
        SHA1_ROUND_3(b, c, d, e, a, 49);
        SHA1_ROUND_3(a, b, c, d, e, 50);
        SHA1_ROUND_3(e, a, b, c, d, 51);
        SHA1_ROUND_3(d, e, a, b, c, 52);
        SHA1_ROUND_3(c, d, e, a, b, 53);
        SHA1_ROUND_3(b, c, d, e, a, 54);
        SHA1_ROUND_3(a, b, c, d, e, 55);
        SHA1_ROUND_3(e, a, b, c, d, 56);
        SHA1_ROUND_3(d, e, a, b, c, 57);
        SHA1_ROUND_3(c, d, e, a, b, 58);
        SHA1_ROUND_3(b, c, d, e, a, 59);
        SHA1_ROUND_4(a, b, c, d, e, 60);
        SHA1_ROUND_4(e, a, b, c, d, 61);
        SHA1_ROUND_4(d, e, a, b, c, 62);
        SHA1_ROUND_4(c, d, e, a, b, 63);
        SHA1_ROUND_4(b, c, d, e, a, 64);
        SHA1_ROUND_4(a, b, c, d, e, 65);
        SHA1_ROUND_4(e, a, b, c, d, 66);
        SHA1_ROUND_4(d, e, a, b, c, 67);
        SHA1_ROUND_4(c, d, e, a, b, 68);
        SHA1_ROUND_4(b, c, d, e, a, 69);
        SHA1_ROUND_4(a, b, c, d, e, 70);
        SHA1_ROUND_4(e, a, b, c, d, 71);
        SHA1_ROUND_4(d, e, a, b, c, 72);
        SHA1_ROUND_4(c, d, e, a, b, 73);
        SHA1_ROUND_4(b, c, d, e, a, 74);
        SHA1_ROUND_4(a, b, c, d, e, 75);
        SHA1_ROUND_4(e, a, b, c, d, 76);
        SHA1_ROUND_4(d, e, a, b, c, 77);
        SHA1_ROUND_4(c, d, e, a, b, 78);
        SHA1_ROUND_4(b, c, d, e, a, 79);

#undef SHA1_LOAD
#undef SHA1_ROUND_0
#undef SHA1_ROUND_1
#undef SHA1_ROUND_2
#undef SHA1_ROUND_3
#undef SHA1_ROUND_4

        m_State[0] += a;
        m_State[1] += b;
        m_State[2] += c;
        m_State[3] += d;
        m_State[4] += e;
    }
#pragma endregion

#pragma region PersistentCache
    PersistentCache::~PersistentCache()
    {
        SAFE_DELETE(m_pFileReader);
    }


    const PersistentCache::Stats& PersistentCache::GetStats() const
    {
        return m_stats;
    }

    void PersistentCache::ResetStats()
    {
        m_stats = {};
    }

    bool PersistentCache::ReadEntry(const Key& passkey, const Key& passShaderTextkey, KGFX_IBlob** outData)
    {
        bool   bRes = false;
        bool   bRet = false;
        IFile* cacheFile = nullptr;

        ++m_stats.missCount;
        ScopedAllocation      out = {};
        std::filesystem::path cachePath = GetCurrentCachePath(passkey.toString());

        std::lock_guard<std::mutex> mutexLock(m_ShaderCacheMutex);
        bRes = m_pFileReader->CacheFileExit(cachePath.string().c_str());
        KG_PROCESS_ERROR(bRes);

        cacheFile = static_cast<IFile*>(m_pFileReader->OpenFile(cachePath.string().c_str()));
        KG_PROCESS_ERROR(cacheFile);


        bRes = ParsingAndCheckCacheFile(cacheFile, passShaderTextkey, out);
        KG_PROCESS_ERROR(bRes);

        --m_stats.missCount;
        ++m_stats.hitCount;
        *outData = RawBlob::MoveCreate(out);

        bRet = true;
    Exit0:
        m_pFileReader->CloseFile(cacheFile);
        return bRet;
    }

    bool PersistentCache::WriteEntry(const Key& passkey, const Key& passShaderTextkey, KGFX_IBlob* data)
    {

        ShaderCacheIndexHeader cacheIndex = {};
        std::filesystem::path cachePath = GetCurrentCachePath(passkey.toString());
        cacheIndex.BlobSize = static_cast<uint32_t>(data->GetBufferSize());
        cacheIndex.CompilerHash = passShaderTextkey;

        DigestBuilder<SHA1> dataHashSHA1 = {};
        dataHashSHA1.Append(data);
        cacheIndex.CacheHash = dataHashSHA1.finalize();

        bool bRes = WriteFile<true>(cacheIndex, cachePath, data);

#ifdef _DEBUG
        std::string newName = cachePath.stem().string(); // 去掉扩展
        newName += "_AfterMath";
        newName += cachePath.extension().string();
        std::filesystem::path afterMathPath = cachePath;
        afterMathPath.replace_filename(newName);

        bRes = WriteFile<false>(cacheIndex, afterMathPath, data);
#endif
        return bRes;
    }

    bool PersistentCache::ReadPipelineCache(KGFX_IBlob** outData)
    {
        bool   bRes = false;
        bool   bRet = false;
        IFile* cacheFile = nullptr;

        ++m_stats.missCount;
        ScopedAllocation out = {};

        std::filesystem::path cachePath = GetPipelineCachePath();

        std::lock_guard<std::mutex> mutexLock(m_PipelineCacheMutex);
        bRes = m_pFileReader->CacheFileExit(cachePath.string().c_str());
        KG_PROCESS_ERROR(bRes);

        cacheFile = static_cast<IFile*>(m_pFileReader->OpenFile(cachePath.string().c_str()));
        KG_PROCESS_ERROR(cacheFile);


        bRes = ParsingAndCheckPipelineCache(cacheFile, out);
        KG_PROCESS_ERROR(bRes);

        *outData = RawBlob::MoveCreate(out);
        bRet = true;
    Exit0:
        m_pFileReader->CloseFile(cacheFile);
        return bRet;
    }

    bool PersistentCache::ParsingAndCheckCacheFile(IFile* file, const Key& passShaderTextkey, ScopedAllocation& out) const
    {
        ShaderCacheIndexHeader cacheIndex = {};
        cacheIndex.CompilerHash = passShaderTextkey;

        bool bRes = ParsingAndCheckFile(file, out, cacheIndex);

        return bRes;
    }

    bool PersistentCache::ParsingAndCheckPipelineCache(IFile* file, ScopedAllocation& out) const
    {
        auto gfxDeviceFingerprint = KGFX_GetGraphicDeviceDx12Internal()->GetAdapterFingerprint();
        PipelineCacheFileHeaderV2 expectedHeader = {};
        expectedHeader.VendorId = gfxDeviceFingerprint.vendorId;
        expectedHeader.DeviceId = gfxDeviceFingerprint.deviceId;
        expectedHeader.SubSysId = gfxDeviceFingerprint.subSysId;
        expectedHeader.Revision = gfxDeviceFingerprint.revision;
        expectedHeader.AdapterLuidLow = gfxDeviceFingerprint.luidLow;
        expectedHeader.AdapterLuidHigh = gfxDeviceFingerprint.luidHigh;
        expectedHeader.DriverVersion = gfxDeviceFingerprint.driverVersion;

        bool bRes = ParsingAndCheckFile(file, out, expectedHeader);

        return bRes;
    }

    bool PersistentCache::WritePipelineCache(KGFX_IBlob* data)
    {
        auto gfxDeviceFingerprint = KGFX_GetGraphicDeviceDx12Internal()->GetAdapterFingerprint();
        PipelineCacheFileHeaderV2 cacheIndex = {};
        DigestBuilder<SHA1> shaderTextbuilder = {};
        std::filesystem::path cachePath = GetPipelineCachePath();
        shaderTextbuilder.Append(data);

        cacheIndex.VendorId = gfxDeviceFingerprint.vendorId;
        cacheIndex.DeviceId = gfxDeviceFingerprint.deviceId;
        cacheIndex.SubSysId = gfxDeviceFingerprint.subSysId;
        cacheIndex.Revision = gfxDeviceFingerprint.revision;
        cacheIndex.AdapterLuidLow = gfxDeviceFingerprint.luidLow;
        cacheIndex.AdapterLuidHigh = gfxDeviceFingerprint.luidHigh;
        cacheIndex.DriverVersion = gfxDeviceFingerprint.driverVersion;
        cacheIndex.BlobSize = static_cast<uint32_t>(data->GetBufferSize());
        cacheIndex.CacheHash = shaderTextbuilder.finalize();


        return WriteFile<true>(cacheIndex, cachePath, data);
    }


    std::filesystem::path PersistentCache::GetCurrentCachePath(const std::string& _szShaderName) const
    {
        const std::string platform = "win";
        const std::string folder = std::string("shaderbin_") + platform + (m_bDebug ? "_d" : "");
        std::filesystem::path p = CacheDirectoryDX12;
        p /= folder;
        p /= _szShaderName + ".cso";
        return p;
    }

    std::filesystem::path PersistentCache::GetPipelineCachePath() const
    {
        std::filesystem::path cachePath = {};
        std::string           folderName = {};
        std::string           platform = "win";

        if constexpr (m_bDebug)
        {
            folderName = std::string("pipelinebin_") + platform + "_d";
        }
        else
        {
            folderName = std::string("pipelinebin_") + platform;
        }

        cachePath = cachePath / CacheDirectoryDX12 / folderName / "pipelineCache";
        return cachePath;
    }

    void PersistentCache::Initialize()
    {
        if (m_pFileReader == nullptr)
        {
            m_pFileReader = (new ShaderCacheFileImpl);
        }
    }

#pragma endregion

} // namespace gfx
