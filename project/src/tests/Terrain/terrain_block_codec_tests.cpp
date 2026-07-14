#include "doctest.h"

#include "Function/Asset/TerrainBlockCodec.h"

#include <cstdint>
#include <limits>
#include <vector>

TEST_CASE("Terrain block codec round-trips deterministic maximal byte runs")
{
	std::vector<uint8_t> encoded{ 0xffu };
	REQUIRE(AshEngine::encode_terrain_rle({}, encoded));
	CHECK(encoded.empty());

	std::vector<uint8_t> decoded{ 0xffu };
	REQUIRE(AshEngine::decode_terrain_rle(encoded, 0u, decoded));
	CHECK(decoded.empty());

	const std::vector<uint8_t> repeated{ 0x7au, 0x7au, 0x7au, 0x7au };
	REQUIRE(AshEngine::encode_terrain_rle(repeated, encoded));
	CHECK(encoded == std::vector<uint8_t>{ 4u, 0u, 0u, 0u, 0x7au });
	REQUIRE(AshEngine::decode_terrain_rle(encoded, repeated.size(), decoded));
	CHECK(decoded == repeated);

	const std::vector<uint8_t> mixed{
		0x11u, 0x11u,
		0x22u, 0x22u, 0x22u,
		0x11u
	};
	const std::vector<uint8_t> expected{
		2u, 0u, 0u, 0u, 0x11u,
		3u, 0u, 0u, 0u, 0x22u,
		1u, 0u, 0u, 0u, 0x11u
	};
	REQUIRE(AshEngine::encode_terrain_rle(mixed, encoded));
	CHECK(encoded == expected);
	REQUIRE(AshEngine::decode_terrain_rle(encoded, mixed.size(), decoded));
	CHECK(decoded == mixed);
}

TEST_CASE("Terrain block codec bounds smaller candidates and streams the shared RLE")
{
	std::vector<uint8_t> incompressible(4096u);
	for (size_t index = 0u; index < incompressible.size(); ++index)
	{
		incompressible[index] = static_cast<uint8_t>(index);
	}
	std::vector<uint8_t> candidate{ 0xffu };
	REQUIRE(AshEngine::encode_terrain_rle_if_smaller(incompressible, candidate));
	CHECK(candidate.empty());

	const std::vector<uint8_t> repeated(4096u, 0x5au);
	REQUIRE(AshEngine::encode_terrain_rle_if_smaller(repeated, candidate));
	CHECK(candidate == std::vector<uint8_t>{ 0u, 16u, 0u, 0u, 0x5au });
	uint64_t encoded_size = std::numeric_limits<uint64_t>::max();
	REQUIRE(AshEngine::terrain_rle_encoded_size(
		repeated.data(), repeated.size(), encoded_size));
	CHECK(encoded_size == candidate.size());
	std::vector<uint8_t> streamed{};
	REQUIRE(AshEngine::stream_terrain_rle(
		repeated.data(), repeated.size(),
		[&](const uint8_t* bytes, size_t size)
		{
			streamed.insert(streamed.end(), bytes, bytes + size);
			return true;
		}));
	CHECK(streamed == candidate);
}

TEST_CASE("Terrain block codec rejects malformed records without changing output")
{
	const std::vector<uint8_t> sentinel{ 9u, 8u, 7u };

	auto CheckRejected = [&](const std::vector<uint8_t>& encoded, size_t expected_size)
	{
		std::vector<uint8_t> output = sentinel;
		CHECK_FALSE(AshEngine::decode_terrain_rle(encoded, expected_size, output));
		CHECK(output == sentinel);
	};

	CheckRejected({ 0u, 0u, 0u, 0u, 0x5au }, 0u);
	CheckRejected({ 1u, 0u, 0u, 0u }, 1u);
	CheckRejected({ 4u, 0u, 0u, 0u, 0x5au }, 3u);
	CheckRejected({ 2u, 0u, 0u, 0u, 0x5au }, 3u);
	CheckRejected({ 1u, 0u, 0u, 0u, 0x5au, 0xffu }, 1u);
}
