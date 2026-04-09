#pragma once
#include "Base/hcore.h"
#include <vector>
#include <string>
#include <filesystem>
namespace RHI
{
	inline constexpr const char kMagic[4] = { 'S', 'L', 'S', '$' };
	inline constexpr uint32_t kVersion = 1;
	inline std::filesystem::path CacheDirectoryDX12 = "product/caches/ShaderCaches/dx12";
	inline std::filesystem::path CacheDirectoryVulkan = "product/caches/ShaderCaches/vulkan";

	struct DigestUtil
	{
		static int get_hex_digit_value(char c);
		static std::string digest_to_string(void const* digest, int digestSize);
		static bool string_to_digest(const char* _str, int strLength, void* digest, int digestSize);
	};
	struct ShaderCacheFileImpl
	{
		void* open_file(const char* szFilePath) const;
		bool close_file(void* hfile) const;
		unsigned int read(void* hFile, void* pBuffer, unsigned int uReadBytes) const;
		unsigned int write(void* hFile, void* pBuffer, unsigned int uWriteBytes) const;
		int seek(void* hFile, int offset, int origin) const;
		bool cache_file_exit(const char* szFilePath) const; 
	};
	template <int N>
	class HashDigest
	{
	public:
		static_assert(N % 4 == 0, "size must be multiple of 4");
		uint32_t data[N / 4] = { 0 };

		HashDigest() = default;

		HashDigest(const char* str) { DigestUtil::string_to_digest(str, strlen(str), data, N); }

		HashDigest(const std::string& str)
		{
			DigestUtil::string_to_digest(str.data(), str.size(), data, N);
		}

		std::string to_string() const { return DigestUtil::digest_to_string(data, N); }

		bool operator==(const HashDigest& other) const
		{
			return ::memcmp(data, other.data, sizeof(data)) == 0;
		}

		bool operator!=(const HashDigest& other) const { return !(*this == other); }

		uint32_t get_hash_code() const { return data[0]; }
	};
	/// SHA1 hash generator implementing https://www.ietf.org/rfc/rfc3174.txt
	class SHA1
	{
	public:
		using Digest = HashDigest<20>;

		SHA1();

		void init();

		void update(const void* data, size_t size);

		Digest finalize();

		static Digest compute(const void* data, int size);

	private:
		void add_byte(uint8_t x);
		void process_block(const uint8_t* ptr);

		uint32_t m_index = {};
		uint64_t m_bits = {};
		uint32_t m_State[5] = {};
		uint8_t m_buf[64] = {};
	};
	// Helper class for building hashes.
	template <typename Hash>
	struct DigestBuilder
	{
		void append(const void* data, int64_t size) { m_hash.update(data, size); }

		template <typename T, std::enable_if_t<std::is_arithmetic_v<T> || std::is_enum_v<T>, int> = 0>
		void append(const T value)
		{
			append(&value, sizeof(T));
		}

		void append(const std::string& str) { append(str.data(), str.size()); }

		void append(const std::wstring& str) { append(str.data(), str.size()); }

		template <int64_t N>
		void append(const HashDigest<N>& digest)
		{
			append(digest.data, sizeof(digest.data));
		}

		template <typename T, std::enable_if_t<std::has_unique_object_representations_v<T>, int> = 0>
		void append(const std::vector<T>& list)
		{
			append(list.data(), list.size() * sizeof(T));
		}


		typename Hash::Digest finalize() { return m_hash.finalize(); }

		Hash m_hash;
	};

#pragma pack(push,1)
	struct ShaderCacheIndexHeader
	{
		char Magic[4] = { kMagic[0],kMagic[1], kMagic[2], kMagic[3] };
		uint32_t Version = kVersion;
		uint32_t BlobSize = {};
		SHA1::Digest CompilerHash = {}; /// Hash of shader-compiler key inputs.
		SHA1::Digest CacheHash = {};    /// Hash of cached file contents for cache validation.

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


	class ShaderCache
	{
	};
}
