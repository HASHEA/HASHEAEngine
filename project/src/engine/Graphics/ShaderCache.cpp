#include "Base/hcore.h"
#include "ShaderCache.h"
#include <vector>
namespace RHI
{
	int DigestUtil::get_hex_digit_value(char c)
	{
		return 0;
	}
	std::string DigestUtil::digest_to_string(void const* digest, int digestSize)
	{
		return std::string();
	}
	bool DigestUtil::string_to_digest(const char* _str, int strLength, void* digest, int digestSize)
	{
		return false;
	}
	void* ShaderCacheFileImpl::open_file(const char* szFilePath) const
	{
		return nullptr;
	}
	bool ShaderCacheFileImpl::close_file(void* hfile) const
	{
		return false;
	}
	unsigned int ShaderCacheFileImpl::read(void* hFile, void* pBuffer, unsigned int uReadBytes) const
	{
		return 0;
	}
	unsigned int ShaderCacheFileImpl::write(void* hFile, void* pBuffer, unsigned int uWriteBytes) const
	{
		return 0;
	}
	int ShaderCacheFileImpl::seek(void* hFile, int offset, int origin) const
	{
		return 0;
	}
	bool ShaderCacheFileImpl::cache_file_exit(const char* szFilePath) const
	{
		return false;
	}
	SHA1::SHA1()
	{
	}
	void SHA1::init()
	{
	}
	void SHA1::update(const void* data, size_t size)
	{
		if (!data || size <= 0)
		{
			return;
		}

		const uint8_t* ptr = static_cast<const uint8_t*>(data);

		// Fill up buffer if not full.
		while (size > 0 && m_index != 0)
		{
			add_byte(*ptr++);
			m_bits += 8;
			size--;
		}

		// Process full blocks.
		while (size >= sizeof(m_buf))
		{
			process_block(ptr);
			ptr += sizeof(m_buf);
			size -= sizeof(m_buf);
			m_bits += sizeof(m_buf) * 8;
		}

		// Process remaining bytes.
		while (size > 0)
		{
			add_byte(*ptr++);
			m_bits += 8;
			size--;
		}
	}
	SHA1::Digest SHA1::finalize()
	{
		// Finalize with 0x80, some zero padding and the length in bits.
		add_byte(0x80);
		while (m_index % 64 != 56)
		{
			add_byte(0);
		}
		for (int i = 7; i >= 0; --i)
		{
			add_byte(static_cast<uint8_t>(m_bits >> i * 8));
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

	SHA1::Digest SHA1::compute(const void* data, int size)
	{
		SHA1 sha1{};
		sha1.update(data, size);
		return sha1.finalize();
	}
	void SHA1::add_byte(uint8_t x)
	{
		m_buf[m_index++] = x;

		if (m_index >= sizeof(m_buf))
		{
			m_index = 0;
			process_block(m_buf);
		}
	}
	void SHA1::process_block(const uint8_t* ptr)
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
}