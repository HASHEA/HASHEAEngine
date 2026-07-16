#pragma once

#include "Function/Asset/VegetationCodec.h"
#include "Function/Asset/VegetationLayer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AshEngine::VegetationAssetCodecInternal
{
	inline bool fail(std::string* out_error, const std::string& message)
	{
		if (out_error != nullptr)
		{
			*out_error = message;
		}
		return false;
	}

	inline void clear_error(std::string* out_error)
	{
		if (out_error != nullptr)
		{
			out_error->clear();
		}
	}

	inline bool checked_add(const uint64_t lhs, const uint64_t rhs, uint64_t& out)
	{
		if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
		{
			return false;
		}
		out = lhs + rhs;
		return true;
	}

	inline bool checked_mul(const uint64_t lhs, const uint64_t rhs, uint64_t& out)
	{
		if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs)
		{
			return false;
		}
		out = lhs * rhs;
		return true;
	}

	template <size_t Size>
	inline bool all_zero(const std::array<uint8_t, Size>& bytes)
	{
		return std::all_of(bytes.begin(), bytes.end(), [](const uint8_t value)
		{
			return value == 0;
		});
	}

	inline bool valid_utf8(const std::string_view text)
	{
		size_t index = 0;
		while (index < text.size())
		{
			const uint8_t first = static_cast<uint8_t>(text[index]);
			if (first <= 0x7f)
			{
				if (first == 0)
				{
					return false;
				}
				++index;
				continue;
			}
			size_t count = 0;
			uint32_t codepoint = 0;
			uint32_t minimum = 0;
			if ((first & 0xe0u) == 0xc0u)
			{
				count = 2; codepoint = first & 0x1fu; minimum = 0x80u;
			}
			else if ((first & 0xf0u) == 0xe0u)
			{
				count = 3; codepoint = first & 0x0fu; minimum = 0x800u;
			}
			else if ((first & 0xf8u) == 0xf0u)
			{
				count = 4; codepoint = first & 0x07u; minimum = 0x10000u;
			}
			else
			{
				return false;
			}
			if (count > text.size() - index)
			{
				return false;
			}
			for (size_t continuation = 1; continuation < count; ++continuation)
			{
				const uint8_t value = static_cast<uint8_t>(text[index + continuation]);
				if ((value & 0xc0u) != 0x80u)
				{
					return false;
				}
				codepoint = (codepoint << 6u) | (value & 0x3fu);
			}
			if (codepoint < minimum || codepoint > 0x10ffffu ||
				(codepoint >= 0xd800u && codepoint <= 0xdfffu))
			{
				return false;
			}
			index += count;
		}
		return true;
	}

	inline bool valid_asset_path(
		const std::string_view path,
		const bool allow_empty,
		const bool require_species_extension = false)
	{
		if (path.empty())
		{
			return allow_empty;
		}
		if (path.size() > 4096 || !valid_utf8(path) || path.front() == '/' ||
			path.back() == '/' || path.find('\\') != std::string::npos ||
			path.find(':') != std::string::npos || path.find("//") != std::string::npos)
		{
			return false;
		}
		size_t begin = 0;
		while (begin < path.size())
		{
			const size_t end = path.find('/', begin);
			const std::string_view component(path.data() + begin,
				(end == std::string::npos ? path.size() : end) - begin);
			if (component.empty() || component == "." || component == "..")
			{
				return false;
			}
			if (end == std::string::npos)
			{
				break;
			}
			begin = end + 1;
		}
		constexpr std::string_view extension = ".AshVegetation";
		return !require_species_extension ||
			(path.size() > extension.size() &&
				std::string_view(path).substr(path.size() - extension.size()) == extension);
	}

	inline bool cost_within_budget(
		const VegetationLoadCost& cost,
		const VegetationLoadBudget& budget)
	{
		return cost.file_bytes <= budget.max_file_bytes &&
			cost.payload_bytes <= budget.max_payload_bytes &&
			cost.decoded_bytes <= budget.max_decoded_bytes &&
			cost.palette_records <= budget.max_palette_records &&
			cost.tile_records <= budget.max_tile_records &&
			cost.instance_records <= budget.max_instance_records;
	}

	class ByteReader
	{
	public:
		explicit ByteReader(const std::vector<uint8_t>& bytes, const size_t offset = 0)
			: m_bytes(bytes), m_offset(offset)
		{
		}

		size_t offset() const { return m_offset; }
		size_t remaining() const { return m_offset <= m_bytes.size() ? m_bytes.size() - m_offset : 0; }
		bool at_end() const { return m_offset == m_bytes.size(); }

		bool read_u8(uint8_t& value)
		{
			if (remaining() < 1) return false;
			value = m_bytes[m_offset++];
			return true;
		}
		bool read_u16(uint16_t& value)
		{
			if (remaining() < 2) return false;
			value = static_cast<uint16_t>(m_bytes[m_offset]) |
				static_cast<uint16_t>(static_cast<uint16_t>(m_bytes[m_offset + 1]) << 8u);
			m_offset += 2;
			return true;
		}
		bool read_u32(uint32_t& value)
		{
			if (remaining() < 4) return false;
			value = 0;
			for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(m_bytes[m_offset + i]) << (i * 8u);
			m_offset += 4;
			return true;
		}
		bool read_u64(uint64_t& value)
		{
			if (remaining() < 8) return false;
			value = 0;
			for (size_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(m_bytes[m_offset + i]) << (i * 8u);
			m_offset += 8;
			return true;
		}
		bool read_i16(int16_t& value)
		{
			uint16_t bits = 0;
			if (!read_u16(bits)) return false;
			value = bits <= 0x7fffu ? static_cast<int16_t>(bits) :
				static_cast<int16_t>(static_cast<int32_t>(bits) - 0x10000);
			return true;
		}
		bool read_i32(int32_t& value)
		{
			uint32_t bits = 0;
			if (!read_u32(bits)) return false;
			value = bits <= 0x7fffffffu ? static_cast<int32_t>(bits) :
				static_cast<int32_t>(static_cast<int64_t>(bits) - 0x100000000ll);
			return true;
		}
		bool read_i64(int64_t& value)
		{
			uint64_t bits = 0;
			if (!read_u64(bits)) return false;
			value = (bits & (uint64_t{ 1 } << 63u)) == 0 ? static_cast<int64_t>(bits) :
				-1 - static_cast<int64_t>(~bits);
			return true;
		}
		template <size_t Size>
		bool read_array(std::array<uint8_t, Size>& value)
		{
			if (remaining() < Size) return false;
			std::copy_n(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset), Size, value.begin());
			m_offset += Size;
			return true;
		}
		bool read_bytes(const size_t count, std::vector<uint8_t>& value)
		{
			if (remaining() < count) return false;
			value.assign(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
				m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + count));
			m_offset += count;
			return true;
		}
		bool read_string(const size_t count, std::string& value)
		{
			if (remaining() < count) return false;
			value.assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset), count);
			m_offset += count;
			return true;
		}
		bool read_string_view(const size_t count, std::string_view& value)
		{
			if (remaining() < count) return false;
			value = std::string_view(
				reinterpret_cast<const char*>(m_bytes.data() + m_offset), count);
			m_offset += count;
			return true;
		}
		struct ByteView
		{
			const uint8_t* data = nullptr;
			size_t size = 0;
		};
		bool read_view(const size_t count, ByteView& value)
		{
			if (remaining() < count) return false;
			value = { m_bytes.data() + m_offset, count };
			m_offset += count;
			return true;
		}
		bool skip(const size_t count)
		{
			if (remaining() < count) return false;
			m_offset += count;
			return true;
		}

	private:
		const std::vector<uint8_t>& m_bytes;
		size_t m_offset = 0;
	};

	enum class VegetationOwnershipDecision : uint8_t
	{
		BudgetRejected,
		OwnershipAdmitted
	};

	inline VegetationOwnershipDecision decide_vegetation_ownership(
		const VegetationLoadCost& cost,
		const VegetationLoadBudget& budget)
	{
		return cost_within_budget(cost, budget) ?
			VegetationOwnershipDecision::OwnershipAdmitted :
			VegetationOwnershipDecision::BudgetRejected;
	}

	inline bool same_load_cost(
		const VegetationLoadCost& lhs,
		const VegetationLoadCost& rhs)
	{
		return lhs.file_bytes == rhs.file_bytes &&
			lhs.payload_bytes == rhs.payload_bytes &&
			lhs.decoded_bytes == rhs.decoded_bytes &&
			lhs.palette_records == rhs.palette_records &&
			lhs.tile_records == rhs.tile_records &&
			lhs.instance_records == rhs.instance_records;
	}

	class ByteWriter
	{
	public:
		void write_u8(const uint8_t value) { bytes.push_back(value); }
		void write_u16(const uint16_t value)
		{
			bytes.push_back(static_cast<uint8_t>(value));
			bytes.push_back(static_cast<uint8_t>(value >> 8u));
		}
		void write_u32(const uint32_t value)
		{
			for (size_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8u)));
		}
		void write_u64(const uint64_t value)
		{
			for (size_t i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8u)));
		}
		void write_i16(const int16_t value) { write_u16(static_cast<uint16_t>(value)); }
		void write_i32(const int32_t value) { write_u32(static_cast<uint32_t>(value)); }
		void write_i64(const int64_t value) { write_u64(static_cast<uint64_t>(value)); }
		template <size_t Size>
		void write_array(const std::array<uint8_t, Size>& value)
		{
			bytes.insert(bytes.end(), value.begin(), value.end());
		}
		void write_bytes(const std::vector<uint8_t>& value)
		{
			bytes.insert(bytes.end(), value.begin(), value.end());
		}
		void write_string(const std::string& value)
		{
			bytes.insert(bytes.end(), value.begin(), value.end());
		}
		std::vector<uint8_t> bytes{};
	};

	inline void write_u32_at(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value)
	{
		for (size_t i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
	}
	inline void write_u64_at(std::vector<uint8_t>& bytes, const size_t offset, const uint64_t value)
	{
		for (size_t i = 0; i < 8; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
	}

	template <size_t HeaderSize>
	inline bool header_crc_matches(
		const std::vector<uint8_t>& bytes,
		const size_t crc_offset,
		const uint32_t expected)
	{
		if (bytes.size() < HeaderSize || crc_offset + 4u > HeaderSize) return false;
		std::array<uint8_t, HeaderSize> copy{};
		std::copy_n(bytes.begin(), HeaderSize, copy.begin());
		std::fill_n(copy.begin() + static_cast<std::ptrdiff_t>(crc_offset), 4, uint8_t{ 0 });
		return vegetation_crc32(copy.data(), copy.size()) == expected;
	}

	struct PaletteEntryView
	{
		VegetationId species_id{};
		VegetationSha256 species_sha256{};
		std::string_view species_asset_path{};
	};

	inline bool read_palette_entry_view(
		ByteReader& reader,
		PaletteEntryView& entry,
		std::string* out_error)
	{
		uint16_t path_bytes = 0;
		uint16_t reserved = 0;
		if (!reader.read_array(entry.species_id) ||
			!reader.read_array(entry.species_sha256) ||
			!reader.read_u16(path_bytes) || !reader.read_u16(reserved) ||
			path_bytes == 0 || path_bytes > 4096 || reserved != 0 ||
			!reader.read_string_view(path_bytes, entry.species_asset_path) ||
			all_zero(entry.species_id) || all_zero(entry.species_sha256) ||
			!valid_asset_path(entry.species_asset_path, false, true))
		{
			return fail(out_error, "Vegetation palette record is invalid.");
		}
		return true;
	}

	inline bool scan_palette_preflight(
		const std::vector<uint8_t>& bytes,
		ByteReader& reader,
		const uint32_t count,
		const bool allow_empty,
		VegetationLoadCost& cost,
		std::string* out_error,
		std::vector<VegetationId>* out_sorted_ids = nullptr)
	{
		(void)bytes;
		if ((!allow_empty && count == 0) || count > 65534)
			return fail(out_error, "Vegetation palette count is invalid.");
		std::vector<std::string_view> paths{};
		paths.reserve(count);
		if (out_sorted_ids != nullptr)
		{
			out_sorted_ids->clear();
			out_sorted_ids->reserve(count);
		}
		VegetationId previous_id{};
		bool have_previous = false;
		for (uint32_t index = 0; index < count; ++index)
		{
			PaletteEntryView entry{};
			if (!read_palette_entry_view(reader, entry, out_error) ||
				(have_previous && !(previous_id < entry.species_id)))
				return fail(out_error, "Vegetation palette entry is invalid or unsorted.");
			paths.push_back(entry.species_asset_path);
			if (out_sorted_ids != nullptr) out_sorted_ids->push_back(entry.species_id);
			uint64_t charge = 0;
			if (!checked_add(48, entry.species_asset_path.size(), charge) ||
				!checked_add(cost.decoded_bytes, charge, cost.decoded_bytes))
				return fail(out_error, "Vegetation palette decoded cost overflowed.");
			previous_id = entry.species_id;
			have_previous = true;
		}
		std::sort(paths.begin(), paths.end());
		if (std::adjacent_find(paths.begin(), paths.end()) != paths.end())
			return fail(out_error, "Vegetation palette path is duplicated.");
		return true;
	}

	inline bool decode_canonical_plane_view(
		const VegetationLayerPlaneCodec codec,
		const ByteReader::ByteView encoded,
		std::array<uint8_t, 1024>& values,
		std::string* out_error)
	{
		values.fill(0);
		if (codec == VegetationLayerPlaneCodec::Raw)
		{
			if (encoded.size != values.size())
				return fail(out_error, "Vegetation Layer Raw plane size is invalid.");
			std::copy_n(encoded.data, values.size(), values.begin());
		}
		else if (codec == VegetationLayerPlaneCodec::Rle)
		{
			if (encoded.size == 0 || encoded.size % 3u != 0)
				return fail(out_error, "Vegetation Layer RLE byte count is invalid.");
			size_t output = 0;
			uint8_t previous_value = 0;
			bool have_previous = false;
			for (size_t offset = 0; offset < encoded.size; offset += 3u)
			{
				const uint16_t run = static_cast<uint16_t>(encoded.data[offset]) |
					static_cast<uint16_t>(static_cast<uint16_t>(encoded.data[offset + 1u]) << 8u);
				const uint8_t value = encoded.data[offset + 2u];
				if (run == 0 || run > values.size() - output ||
					(have_previous && previous_value == value))
					return fail(out_error, "Vegetation Layer RLE run is invalid or non-maximal.");
				std::fill_n(values.begin() + static_cast<std::ptrdiff_t>(output), run, value);
				output += run;
				previous_value = value;
				have_previous = true;
			}
			if (output != values.size())
				return fail(out_error, "Vegetation Layer RLE sum is invalid.");
		}
		else
		{
			return fail(out_error, "Vegetation Layer plane codec is invalid.");
		}
		size_t run_count = 0;
		for (size_t begin = 0; begin < values.size(); ++run_count)
		{
			size_t end = begin + 1u;
			while (end < values.size() && values[end] == values[begin]) ++end;
			begin = end;
		}
		const VegetationLayerPlaneCodec canonical =
			run_count * 3u < values.size() ? VegetationLayerPlaneCodec::Rle :
			VegetationLayerPlaneCodec::Raw;
		if (codec != canonical)
			return fail(out_error, "Vegetation Layer plane codec is noncanonical.");
		return true;
	}

	inline bool validate_palette(
		const std::vector<VegetationPaletteEntry>& palette,
		const bool allow_empty,
		std::string* out_error)
	{
		if ((!allow_empty && palette.empty()) || palette.size() > 65534)
		{
			return fail(out_error, "Vegetation palette count is invalid.");
		}
		std::set<std::string> paths{};
		for (size_t index = 0; index < palette.size(); ++index)
		{
			const VegetationPaletteEntry& entry = palette[index];
			if (all_zero(entry.species_id) || all_zero(entry.species_sha256) ||
				!valid_asset_path(entry.species_asset_path, false, true) ||
				!paths.insert(entry.species_asset_path).second ||
				(index != 0 && !(palette[index - 1].species_id < entry.species_id)))
			{
				return fail(out_error, "Vegetation palette entry is invalid or unsorted.");
			}
		}
		return true;
	}

	inline bool write_palette(
		const std::vector<VegetationPaletteEntry>& palette,
		ByteWriter& writer,
		std::string* out_error)
	{
		if (!validate_palette(palette, true, out_error)) return false;
		for (const VegetationPaletteEntry& entry : palette)
		{
			writer.write_array(entry.species_id);
			writer.write_array(entry.species_sha256);
			writer.write_u16(static_cast<uint16_t>(entry.species_asset_path.size()));
			writer.write_u16(0);
			writer.write_string(entry.species_asset_path);
		}
		return true;
	}

	inline bool read_palette_entry(
		ByteReader& reader,
		VegetationPaletteEntry& entry,
		std::string* out_error)
	{
		uint16_t path_bytes = 0;
		uint16_t reserved = 0;
		if (!reader.read_array(entry.species_id) ||
			!reader.read_array(entry.species_sha256) ||
			!reader.read_u16(path_bytes) || !reader.read_u16(reserved) ||
			path_bytes == 0 || path_bytes > 4096 || reserved != 0 ||
			!reader.read_string(path_bytes, entry.species_asset_path) ||
			all_zero(entry.species_id) || all_zero(entry.species_sha256) ||
			!valid_asset_path(entry.species_asset_path, false, true))
		{
			return fail(out_error, "Vegetation palette record is invalid.");
		}
		return true;
	}

	inline void canonical_plane_encoding(
		const std::array<uint8_t, 1024>& values,
		VegetationLayerPlaneCodec& codec,
		std::vector<uint8_t>& encoded)
	{
		std::vector<uint8_t> rle{};
		rle.reserve(3072);
		size_t begin = 0;
		while (begin < values.size())
		{
			size_t end = begin + 1;
			while (end < values.size() && values[end] == values[begin]) ++end;
			const uint16_t run = static_cast<uint16_t>(end - begin);
			rle.push_back(static_cast<uint8_t>(run));
			rle.push_back(static_cast<uint8_t>(run >> 8u));
			rle.push_back(values[begin]);
			begin = end;
		}
		if (rle.size() < values.size())
		{
			codec = VegetationLayerPlaneCodec::Rle;
			encoded = std::move(rle);
		}
		else
		{
			codec = VegetationLayerPlaneCodec::Raw;
			encoded.assign(values.begin(), values.end());
		}
	}

	inline bool decode_plane_bytes(
		const VegetationLayerPlaneCodec codec,
		const std::vector<uint8_t>& encoded,
		std::array<uint8_t, 1024>& values,
		std::string* out_error)
	{
		values.fill(0);
		if (codec == VegetationLayerPlaneCodec::Raw)
		{
			if (encoded.size() != values.size()) return fail(out_error, "Raw vegetation plane size is invalid.");
			std::copy(encoded.begin(), encoded.end(), values.begin());
		}
		else if (codec == VegetationLayerPlaneCodec::Rle)
		{
			if (encoded.empty() || encoded.size() % 3 != 0) return fail(out_error, "RLE vegetation plane size is invalid.");
			size_t output = 0;
			for (size_t offset = 0; offset < encoded.size(); offset += 3)
			{
				const uint16_t run = static_cast<uint16_t>(encoded[offset]) |
					static_cast<uint16_t>(static_cast<uint16_t>(encoded[offset + 1]) << 8u);
				if (run == 0 || run > values.size() - output) return fail(out_error, "RLE vegetation plane run is invalid.");
				std::fill_n(values.begin() + static_cast<std::ptrdiff_t>(output), run, encoded[offset + 2]);
				output += run;
			}
			if (output != values.size()) return fail(out_error, "RLE vegetation plane is incomplete.");
		}
		else
		{
			return fail(out_error, "Vegetation plane codec is unknown.");
		}

		VegetationLayerPlaneCodec canonical_codec{};
		std::vector<uint8_t> canonical{};
		canonical_plane_encoding(values, canonical_codec, canonical);
		if (canonical_codec != codec || canonical != encoded)
		{
			return fail(out_error, "Vegetation plane encoding is not canonical.");
		}
		return true;
	}
}
