#pragma once
#include <atlcomcli.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <Engine/File.h>
#include "KGFX_RefPtr.h"
#include <d3d12.h>
#include "KGFX_GraphiceDeviceDx12.h"

namespace gfx
{
    inline constexpr const char kMagic[4] = { 'S', 'L', 'S', '$' };
    inline constexpr uint32_t kVersion = 1;
    inline std::filesystem::path CacheDirectoryDX12 = "cacheshader/dx12";

    class ShaderCacheFileImpl
    {
    public:
        void* OpenFile(const char* pcszFilePath) const;

        void* OpenFileV5File(const char* pcszFilePath) const;

        void* OpenAloneFile(const char* pcszFilePath, BOOL bWritable) const;

        bool CloseFile(void* hFile) const;

        unsigned int Read(void* hFile, void* pBuffer, unsigned int uReadBytes) const;

        unsigned int Write(void* hFile, const void* pBuffer, unsigned int uWriteBytes) const;

        int Seek(void* hFile, int Offset, int Origin) const;

        bool CacheFileExit(const char* pcszFilePath) const;
    };

    struct DigestUtil
    {
        static int GetHexDigitValue(char c);
        /// Convert a binary digest to a string (lower-case hexadecimal).
        /// Returned string is double the length of the digest.
        static std::string DigestToString(const void* digest, int digestSize);

        /// Convert a string to a binary digest.
        /// Expects a string of double the length of the digest size in hexadecimal format.
        /// Sets the digest to all zeros if the string is invalid.
        /// Returns true if string was converted successfully.
        static bool StringToDigest(const char* str, int strLength, void* digest, int digestSize);
    };

    template <int N>
    class HashDigest
    {
    public:
        static_assert(N % 4 == 0, "size must be multiple of 4");
        uint32_t data[N / 4] = { 0 };

        HashDigest() = default;

        HashDigest(const char* str) { DigestUtil::StringToDigest(str, strlen(str), data, N); }

        HashDigest(const std::string& str)
        {
            DigestUtil::StringToDigest(str.data(), str.size(), data, N);
        }

        HashDigest(KGFX_IBlob* blob)
        {
            if (blob->GetBufferSize() == N)
            {
                ::memcpy(data, blob->GetBufferPointer(), N);
            }
        }

        std::string toString() const { return DigestUtil::DigestToString(data, N); }

        KGFX_IBlob* toBlob() const { return RawBlob::Create(data, sizeof(data)); }

        bool operator==(const HashDigest& other) const
        {
            return ::memcmp(data, other.data, sizeof(data)) == 0;
        }

        bool operator!=(const HashDigest& other) const { return !(*this == other); }

        uint32_t GetHashCode() const { return data[0]; }
    };

    /// SHA1 hash generator implementing https://www.ietf.org/rfc/rfc3174.txt
    class SHA1
    {
    public:
        using Digest = HashDigest<20>;

        SHA1();

        void Init();

        void Update(const void* data, size_t size);

        Digest Finalize();

        static Digest Compute(const void* data, int size);

    private:
        void AddByte(uint8_t x);
        void ProcessBlock(const uint8_t* ptr);

        uint32_t m_index = {};
        uint64_t m_bits = {};
        uint32_t m_State[5] = {};
        uint8_t m_buf[64] = {};
    };

    // Helper class for building hashes.
    template <typename Hash>
    struct DigestBuilder
    {
        void Append(const void* data, int64_t size) { m_hash.Update(data, size); }

        template <typename T, std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T>, int> = 0>
        void Append(const T value)
        {
            Append(&value, sizeof(T));
        }

        void Append(const std::string& str) { Append(str.data(), str.size()); }

        void Append(const std::wstring& str) { Append(str.data(), str.size()); }

        void Append(KGFX_IBlob* blob) { Append(blob->GetBufferPointer(), blob->GetBufferSize()); }

        template <int64_t N>
        void Append(const HashDigest<N>& digest)
        {
            Append(digest.data, sizeof(digest.data));
        }

        template <typename T, std::enable_if_t<std::has_unique_object_representations_v<T>, int> = 0>
        void Append(const std::vector<T>& list)
        {
            Append(list.data(), list.size() * sizeof(T));
        }

        typename Hash::Digest finalize() { return m_hash.Finalize(); }


        Hash m_hash;
    };

#pragma pack(push,1)
    struct ShaderCacheIndexHeader
    {
        char Magic[4] = { kMagic[0],kMagic[1], kMagic[2], kMagic[3] };
        uint32_t Version = kVersion;
        uint32_t BlobSize = {};
        SHA1::Digest CompilerHash = {}; /// shader编译时候关键字的hash
        SHA1::Digest CacheHash = {};    /// 缓存文件内容的hash，用来校验读取的文件内容是否丢失

        bool operator ==(const ShaderCacheIndexHeader& other) const
        {
            return memcmp(other.Magic, Magic, 4) == 0 &&
                other.Version == Version &&
                other.CompilerHash == CompilerHash;
        }

        bool operator!=(const ShaderCacheIndexHeader& other) const
        {
            return !(*this == other);
        }
    };

    struct PipelineCacheFileHeaderV2
    {
        char     Magic[4] = { kMagic[0],kMagic[1], kMagic[2], kMagic[3] };
        uint16_t Version = kVersion;
        uint32_t VendorId = 0;
        uint32_t DeviceId = 0;
        uint32_t SubSysId = 0;
        uint32_t Revision = 0;
        uint32_t AdapterLuidLow = 0;
        uint32_t AdapterLuidHigh = 0;
        uint64_t DriverVersion = 0;
        SHA1::Digest CompilerHash = {};
        SHA1::Digest CacheHash = {};
        uint32_t BlobSize = 0;

        bool operator ==(const PipelineCacheFileHeaderV2& other) const
        {
            return memcmp(other.Magic, Magic, 4) == 0 &&
                other.Version == Version &&
                other.VendorId == VendorId &&
                other.DeviceId == DeviceId &&
                other.SubSysId == SubSysId &&
                other.Revision == Revision &&
                other.AdapterLuidLow == AdapterLuidLow &&
                other.AdapterLuidHigh == AdapterLuidHigh &&
                other.DriverVersion == DriverVersion;
        }

        bool operator!=(const PipelineCacheFileHeaderV2& other) const
        {
            return !(*this == other);
        }
    };
#pragma pack(pop)




    class PersistentCache
    {
    public:
        struct Stats
        {
            // Number of cache hits since last resetting the stats.
            int hitCount;
            // Number of cache misses since last resetting the stats.
            int missCount;
        };

        using Key = SHA1::Digest;

        PersistentCache() = default;
        ~PersistentCache();
        PersistentCache(const PersistentCache& other) = delete;
        PersistentCache(const PersistentCache&& other) = delete;
        PersistentCache operator =(PersistentCache& other) = delete;
        PersistentCache operator =(PersistentCache&& other) = delete;

        void Initialize();

        const Stats& GetStats() const;

        void ResetStats();

        /**
         * 查询一个pass的chache文件是否存在
         * @param passkey pass的编译时关键字hash
         * @param passShaderTextkey 展开后文本的hash
         * @param outData 返回的cache数据
         * @return
         */
        bool ReadEntry(const Key& passkey, const Key& passShaderTextkey, KGFX_IBlob** outData);


        bool WriteEntry(const Key& key, const Key& passShaderTextkey, KGFX_IBlob* data);

        bool ReadPipelineCache(KGFX_IBlob** outData);

        bool WritePipelineCache(KGFX_IBlob* data);

    private:
        bool ParsingAndCheckCacheFile(IFile* file, const Key& passShaderTextkey, ScopedAllocation& out) const;

        bool ParsingAndCheckPipelineCache(IFile* file, ScopedAllocation& out) const;

        template<typename SF, typename = std::enable_if_t<std::is_default_constructible_v<SF>&& std::is_trivially_copyable_v<SF>>>
        bool ParsingAndCheckFile(IFile* file, ScopedAllocation& out, const SF& checkData) const;

        template<bool needIndex, typename SF, typename = std::enable_if_t<std::is_default_constructible_v<SF>&& std::is_trivially_copyable_v<SF>>>
        bool WriteFile(SF& checkData, const std::filesystem::path& cachePath, KGFX_IBlob* data);

        std::filesystem::path GetCurrentCachePath(const std::string& _szShaderName) const;

        std::filesystem::path GetPipelineCachePath() const;

        ShaderCacheFileImpl* m_pFileReader = nullptr;

        //std::filesystem::path m_CacheDirectory = {"CachedShaders_DX12"};

        std::mutex m_ShaderCacheMutex = {};
        std::mutex m_PipelineCacheMutex = {};
        Stats m_stats = {};


#ifdef _DEBUG
        static constexpr bool m_bDebug = true;
#else
        static constexpr bool m_bDebug = false;
#endif
    };


    template <typename SF, typename>
    bool PersistentCache::ParsingAndCheckFile(IFile* file, ScopedAllocation& out, const SF& checkData)const
    {
        SF cacheIndex = {};
        {
            constexpr uint32_t kMaxBlob = 32u * 1024u * 1024u;
            auto ReadSize = m_pFileReader->Read(file, &cacheIndex, sizeof(std::decay_t<SF>));
            if (ReadSize != sizeof(std::decay_t<SF>) || cacheIndex.BlobSize < 48 || cacheIndex.BlobSize >kMaxBlob)
            {
                return false;
            }
        }


        if (checkData != cacheIndex)
        {
            return false;
        }

        DigestBuilder<SHA1> dataHashSHA1 = {};


        if (file->Size() >= cacheIndex.BlobSize)
        {
            void* data = out.AllocateTerminated(cacheIndex.BlobSize);
            if (data == nullptr)
            {
                return false;
            }

            m_pFileReader->Read(file, data, cacheIndex.BlobSize);
            dataHashSHA1.Append(out.GetData(), out.GetSizeInBytes());

            if (cacheIndex.CacheHash != dataHashSHA1.finalize())
            {
                return false;
            }

            return true;
        }

        return false;
    }

    template <bool needIndex, typename SF, typename>
    bool PersistentCache::WriteFile(SF& checkData, const std::filesystem::path& cachePath, KGFX_IBlob* data)
    {
        struct RAIIFile
        {
            RAIIFile(ShaderCacheFileImpl* FileReader, IFile* File) : writeFile(File), m_pFileReader(FileReader) {}

            ~RAIIFile()
            {
                if (m_pFileReader)
                {
                    m_pFileReader->CloseFile(writeFile);
                }
                writeFile = nullptr;
                m_pFileReader = nullptr;
            }
            bool operator()() const
            {
                return m_pFileReader != nullptr && writeFile != nullptr;
            }
            IFile* writeFile = nullptr;
            ShaderCacheFileImpl* m_pFileReader = nullptr;
        };


        std::lock_guard<std::mutex> mutexLock(m_ShaderCacheMutex);
        RAIIFile writeFile = { m_pFileReader ,static_cast<IFile*>(m_pFileReader->OpenAloneFile(cachePath.string().c_str(), true)) };
        if (!writeFile())
        {
            return false;
        }

        if constexpr (needIndex)
        {
            auto wrote = m_pFileReader->Write(writeFile.writeFile, &checkData, sizeof(SF));
            if (wrote != sizeof(SF))
            {
                return false;
            }
        }

        auto wrote = m_pFileReader->Write(writeFile.writeFile, data->GetBufferPointer(), static_cast<uint32_t>(data->GetBufferSize()));
        if (wrote != data->GetBufferSize())
        {
            return false;
        }

        return true;
    }
}
