#include "Function/Asset/VegetationLayer.h"

#include "Function/Asset/VegetationAssetCodecInternal.h"
#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		using namespace VegetationAssetCodecInternal;
		constexpr size_t k_header_size = 80;
		constexpr std::array<uint8_t, 4> k_magic{ 'A', 'S', 'V', 'L' };

		bool tile_less(const VegetationLayerTile& lhs, const VegetationLayerTile& rhs)
		{
			return lhs.tile_z < rhs.tile_z || (lhs.tile_z == rhs.tile_z && lhs.tile_x < rhs.tile_x);
		}

		bool contains_id(const std::vector<VegetationPaletteEntry>& palette, const VegetationId& id)
		{
			const auto iterator = std::lower_bound(palette.begin(), palette.end(), id,
				[](const VegetationPaletteEntry& entry, const VegetationId& value)
				{
					return entry.species_id < value;
				});
			return iterator != palette.end() && iterator->species_id == id;
		}

		bool validate_plane_sequence(
			const VegetationLayerTile& tile,
			const std::vector<VegetationPaletteEntry>& palette,
			std::string* out_error)
		{
			if (tile.planes.empty() || tile.planes.size() > std::numeric_limits<uint16_t>::max())
				return fail(out_error, "Vegetation Layer plane count is invalid.");
			VegetationId previous_weight{};
			bool have_previous_weight = false;
			for (size_t index = 0; index < tile.planes.size(); ++index)
			{
				const VegetationLayerPlane& plane = tile.planes[index];
				if (std::all_of(plane.values.begin(), plane.values.end(), [](const uint8_t value) { return value == 0; }))
					return fail(out_error, "Vegetation Layer cannot contain an all-zero plane.");
				if (index == 0)
				{
					if (plane.kind != VegetationLayerPlaneKind::Density || !all_zero(plane.species_id))
						return fail(out_error, "Vegetation Layer density plane is invalid.");
				}
				else
				{
					if (plane.kind != VegetationLayerPlaneKind::SpeciesWeight || all_zero(plane.species_id) ||
						!contains_id(palette, plane.species_id) ||
						(have_previous_weight && !(previous_weight < plane.species_id)))
						return fail(out_error, "Vegetation Layer weight plane is invalid or unsorted.");
					previous_weight = plane.species_id;
					have_previous_weight = true;
				}
			}
			return true;
		}

		bool validate_layer(const VegetationLayerSnapshot& layer, std::string* out_error)
		{
			if (all_zero(layer.layer_id) || layer.content_generation == 0 ||
				!validate_palette(layer.palette, true, out_error))
				return fail(out_error, "Vegetation Layer identity or palette is invalid.");
			for (size_t index = 0; index < layer.tiles.size(); ++index)
			{
				if ((index != 0 && !tile_less(layer.tiles[index - 1], layer.tiles[index])) ||
					!validate_plane_sequence(layer.tiles[index], layer.palette, out_error))
					return fail(out_error, "Vegetation Layer tiles are invalid or unsorted.");
			}
			return true;
		}

		bool add_layer_cost_palette(VegetationLoadCost& cost, const VegetationPaletteEntry& entry)
		{
			uint64_t charge = 0;
			return checked_add(48, entry.species_asset_path.size(), charge) &&
				checked_add(cost.decoded_bytes, charge, cost.decoded_bytes);
		}

		bool preflight_layer(
			const std::vector<uint8_t>& bytes,
			const VegetationLoadBudget& budget,
			VegetationLoadCost& cost,
			std::string* out_error)
		{
			cost = {};
			if (bytes.size() < k_header_size)
				return fail(out_error, "Vegetation Layer file is smaller than its header.");
			if (bytes.size() > budget.max_file_bytes)
			{
				cost.file_bytes = bytes.size();
				return fail(out_error, "Vegetation Layer file budget exceeded before ownership.");
			}
			ByteReader header(bytes);
			std::array<uint8_t, 4> magic{};
			uint16_t version = 0, header_size = 0;
			uint32_t flags = 0, tile_resolution = 0, tile_size_cm = 0;
			uint32_t palette_count = 0, tile_count = 0, payload_crc = 0, header_crc = 0, reserved = 0;
			uint64_t generation = 0, seed = 0, payload_bytes = 0;
			VegetationId layer_id{};
			if (!header.read_array(magic) || !header.read_u16(version) || !header.read_u16(header_size) ||
				!header.read_u32(flags) || !header.read_u32(tile_resolution) || !header.read_u32(tile_size_cm) ||
				!header.read_u32(palette_count) || !header.read_u64(generation) || !header.read_u64(seed) ||
				!header.read_array(layer_id) || !header.read_u32(tile_count) || !header.read_u64(payload_bytes) ||
				!header.read_u32(payload_crc) || !header.read_u32(header_crc) || !header.read_u32(reserved) ||
				magic != k_magic || version != 1 || header_size != k_header_size || flags != 0 ||
				tile_resolution != 32 || tile_size_cm != 3200 || palette_count > 65534 || generation == 0 ||
				all_zero(layer_id) || reserved != 0 || payload_bytes != bytes.size() - k_header_size)
				return fail(out_error, "Vegetation Layer header is invalid.");
			cost.file_bytes = bytes.size();
			cost.payload_bytes = payload_bytes;
			cost.decoded_bytes = 32;
			cost.palette_records = palette_count;
			cost.tile_records = tile_count;
			if (payload_bytes > budget.max_payload_bytes ||
				palette_count > budget.max_palette_records || tile_count > budget.max_tile_records)
				return fail(out_error, "Vegetation Layer count or payload budget exceeded before ownership.");
			if (!header_crc_matches<k_header_size>(bytes, 72, header_crc) ||
				vegetation_crc32(bytes.data() + k_header_size, bytes.size() - k_header_size) != payload_crc)
				return fail(out_error, "Vegetation Layer CRC is invalid.");
			uint64_t minimum_palette = 0, minimum_tiles = 0, minimum_payload = 0;
			if (!checked_mul(palette_count, 53, minimum_palette) ||
				!checked_mul(tile_count, 59, minimum_tiles) ||
				!checked_add(minimum_palette, minimum_tiles, minimum_payload) || minimum_payload > payload_bytes)
				return fail(out_error, "Vegetation Layer declared counts exceed its payload.");

			ByteReader reader(bytes, k_header_size);
			std::vector<VegetationId> palette_ids{};
			if (!scan_palette_preflight(bytes, reader, palette_count, true, cost, out_error, &palette_ids)) return false;
			int64_t previous_x = 0, previous_z = 0;
			bool have_previous_tile = false;
			for (uint32_t tile_index = 0; tile_index < tile_count; ++tile_index)
			{
				int64_t tile_x = 0, tile_z = 0;
				uint16_t plane_count = 0, tile_reserved = 0;
				uint32_t record_bytes = 0;
				if (!reader.read_i64(tile_x) || !reader.read_i64(tile_z) ||
					!reader.read_u16(plane_count) || !reader.read_u16(tile_reserved) ||
					!reader.read_u32(record_bytes) || plane_count == 0 || tile_reserved != 0 ||
					record_bytes > reader.remaining() ||
					(have_previous_tile &&
						(tile_z < previous_z || (tile_z == previous_z && tile_x <= previous_x))))
					return fail(out_error, "Vegetation Layer tile header or ordering is invalid.");
				const size_t record_begin = reader.offset();
				uint64_t plane_charge = 0;
				if (!checked_mul(plane_count, 1041, plane_charge) ||
					!checked_add(cost.decoded_bytes, 16, cost.decoded_bytes) ||
					!checked_add(cost.decoded_bytes, plane_charge, cost.decoded_bytes))
					return fail(out_error, "Vegetation Layer decoded cost overflowed.");
				VegetationId previous_weight{};
				bool have_previous_weight = false;
				for (uint16_t plane_index = 0; plane_index < plane_count; ++plane_index)
				{
					uint8_t kind_value = 0, codec_value = 0;
					uint16_t plane_reserved = 0;
					uint32_t decoded_bytes = 0, encoded_bytes = 0, decoded_crc = 0;
					VegetationId species_id{};
					ByteReader::ByteView encoded{};
					if (!reader.read_u8(kind_value) || !reader.read_u8(codec_value) ||
						!reader.read_u16(plane_reserved) || !reader.read_array(species_id) ||
						!reader.read_u32(decoded_bytes) || !reader.read_u32(encoded_bytes) ||
						!reader.read_u32(decoded_crc) || plane_reserved != 0 || decoded_bytes != 1024 ||
						encoded_bytes == 0 || encoded_bytes > 3072 || !reader.read_view(encoded_bytes, encoded))
						return fail(out_error, "Vegetation Layer plane header is invalid.");
					const auto kind = static_cast<VegetationLayerPlaneKind>(kind_value);
					const auto codec = static_cast<VegetationLayerPlaneCodec>(codec_value);
					std::array<uint8_t, 1024> values{};
					if (!decode_canonical_plane_view(codec, encoded, values, out_error) ||
						vegetation_crc32(values.data(), values.size()) != decoded_crc || all_zero(values))
						return fail(out_error, "Vegetation Layer plane data is invalid or noncanonical.");
					if (plane_index == 0)
					{
						if (kind != VegetationLayerPlaneKind::Density || !all_zero(species_id))
							return fail(out_error, "Vegetation Layer density plane is invalid.");
					}
					else if (kind != VegetationLayerPlaneKind::SpeciesWeight || all_zero(species_id) ||
						!std::binary_search(palette_ids.begin(), palette_ids.end(), species_id) ||
						(have_previous_weight && !(previous_weight < species_id)))
					{
						return fail(out_error, "Vegetation Layer weight plane is invalid or unsorted.");
					}
					else
					{
						previous_weight = species_id;
						have_previous_weight = true;
					}
				}
				if (reader.offset() - record_begin != record_bytes)
					return fail(out_error, "Vegetation Layer tile record size is invalid.");
				previous_x = tile_x;
				previous_z = tile_z;
				have_previous_tile = true;
			}
			if (!reader.at_end()) return fail(out_error, "Vegetation Layer payload has trailing bytes.");
			if (decide_vegetation_ownership(cost, budget) == VegetationOwnershipDecision::BudgetRejected)
				return fail(out_error, "Vegetation Layer decoded budget exceeded before ownership.");
			return true;
		}
	}

	bool encode_vegetation_layer(
		const VegetationLayerSnapshot& layer,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error)
	{
		out_bytes.clear();
		clear_error(out_error);
		if (!validate_layer(layer, out_error) ||
			layer.palette.size() > std::numeric_limits<uint32_t>::max() ||
			layer.tiles.size() > std::numeric_limits<uint32_t>::max()) return false;

		ByteWriter payload{};
		if (!write_palette(layer.palette, payload, out_error)) return false;
		for (const VegetationLayerTile& tile : layer.tiles)
		{
			ByteWriter records{};
			for (const VegetationLayerPlane& plane : tile.planes)
			{
				VegetationLayerPlaneCodec codec{};
				std::vector<uint8_t> encoded{};
				canonical_plane_encoding(plane.values, codec, encoded);
				records.write_u8(static_cast<uint8_t>(plane.kind));
				records.write_u8(static_cast<uint8_t>(codec));
				records.write_u16(0);
				records.write_array(plane.species_id);
				records.write_u32(1024);
				records.write_u32(static_cast<uint32_t>(encoded.size()));
				records.write_u32(vegetation_crc32(plane.values.data(), plane.values.size()));
				records.write_bytes(encoded);
			}
			if (records.bytes.size() > std::numeric_limits<uint32_t>::max())
				return fail(out_error, "Vegetation Layer tile record is too large.");
			payload.write_i64(tile.tile_x);
			payload.write_i64(tile.tile_z);
			payload.write_u16(static_cast<uint16_t>(tile.planes.size()));
			payload.write_u16(0);
			payload.write_u32(static_cast<uint32_t>(records.bytes.size()));
			payload.write_bytes(records.bytes);
		}

		ByteWriter header{};
		header.write_array(k_magic);
		header.write_u16(1);
		header.write_u16(static_cast<uint16_t>(k_header_size));
		header.write_u32(0);
		header.write_u32(32);
		header.write_u32(3200);
		header.write_u32(static_cast<uint32_t>(layer.palette.size()));
		header.write_u64(layer.content_generation);
		header.write_u64(layer.layer_seed);
		header.write_array(layer.layer_id);
		header.write_u32(static_cast<uint32_t>(layer.tiles.size()));
		header.write_u64(payload.bytes.size());
		header.write_u32(vegetation_crc32(payload.bytes.data(), payload.bytes.size()));
		header.write_u32(0);
		header.write_u32(0);
		if (header.bytes.size() != k_header_size)
			return fail(out_error, "Vegetation Layer header construction failed.");
		write_u32_at(header.bytes, 72, vegetation_crc32(header.bytes.data(), header.bytes.size()));
		header.bytes.insert(header.bytes.end(), payload.bytes.begin(), payload.bytes.end());
		out_bytes = std::move(header.bytes);
		clear_error(out_error);
		return true;
	}

	bool decode_vegetation_layer(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationLayerSnapshot& out_layer,
		std::string* out_error,
		VegetationLoadCost* out_cost)
	{
		out_layer = {};
		if (out_cost != nullptr) *out_cost = {};
		clear_error(out_error);
		if (bytes.size() < k_header_size || bytes.size() > budget.max_file_bytes)
			return fail(out_error, "Vegetation Layer file size exceeds its budget or header.");
		VegetationLoadCost preflight_cost{};
		if (!preflight_layer(bytes, budget, preflight_cost, out_error)) return false;

		ByteReader header(bytes);
		std::array<uint8_t, 4> magic{};
		uint16_t version = 0, header_size = 0;
		uint32_t flags = 0, tile_resolution = 0, tile_size_cm = 0;
		uint32_t palette_count = 0, tile_count = 0, payload_crc = 0, header_crc = 0, reserved = 0;
		uint64_t generation = 0, seed = 0, payload_bytes = 0;
		VegetationId layer_id{};
		if (!header.read_array(magic) || !header.read_u16(version) || !header.read_u16(header_size) ||
			!header.read_u32(flags) || !header.read_u32(tile_resolution) || !header.read_u32(tile_size_cm) ||
			!header.read_u32(palette_count) || !header.read_u64(generation) || !header.read_u64(seed) ||
			!header.read_array(layer_id) || !header.read_u32(tile_count) || !header.read_u64(payload_bytes) ||
			!header.read_u32(payload_crc) || !header.read_u32(header_crc) || !header.read_u32(reserved) ||
			magic != k_magic || version != 1 || header_size != k_header_size || flags != 0 ||
			tile_resolution != 32 || tile_size_cm != 3200 || palette_count > 65534 || generation == 0 ||
			all_zero(layer_id) || reserved != 0 || payload_bytes != bytes.size() - k_header_size ||
			payload_bytes > budget.max_payload_bytes || palette_count > budget.max_palette_records ||
			tile_count > budget.max_tile_records)
			return fail(out_error, "Vegetation Layer header is invalid.");

		std::vector<uint8_t> header_copy(bytes.begin(), bytes.begin() + k_header_size);
		write_u32_at(header_copy, 72, 0);
		if (vegetation_crc32(header_copy.data(), header_copy.size()) != header_crc ||
			vegetation_crc32(bytes.data() + k_header_size, bytes.size() - k_header_size) != payload_crc)
			return fail(out_error, "Vegetation Layer CRC is invalid.");

		uint64_t minimum_palette = 0, minimum_tiles = 0, minimum_payload = 0;
		if (!checked_mul(palette_count, 53, minimum_palette) ||
			!checked_mul(tile_count, 59, minimum_tiles) ||
			!checked_add(minimum_palette, minimum_tiles, minimum_payload) || minimum_payload > payload_bytes)
			return fail(out_error, "Vegetation Layer declared counts exceed its payload.");

		VegetationLayerSnapshot layer{};
		layer.layer_id = layer_id;
		layer.content_generation = generation;
		layer.layer_seed = seed;
		VegetationLoadCost cost{};
		cost.file_bytes = bytes.size();
		cost.payload_bytes = payload_bytes;
		cost.decoded_bytes = 32;
		cost.palette_records = palette_count;
		cost.tile_records = tile_count;
		ByteReader reader(bytes, k_header_size);
		layer.palette.reserve(palette_count);
		for (uint32_t index = 0; index < palette_count; ++index)
		{
			VegetationPaletteEntry entry{};
			if (!read_palette_entry(reader, entry, out_error)) return false;
			if (!add_layer_cost_palette(cost, entry) || cost.decoded_bytes > budget.max_decoded_bytes)
				return fail(out_error, "Vegetation Layer decoded budget exceeded.");
			layer.palette.push_back(std::move(entry));
		}
		if (!validate_palette(layer.palette, true, out_error)) return false;

		layer.tiles.reserve(tile_count);
		for (uint32_t tile_index = 0; tile_index < tile_count; ++tile_index)
		{
			VegetationLayerTile tile{};
			uint16_t plane_count = 0, tile_reserved = 0;
			uint32_t record_bytes = 0;
			if (!reader.read_i64(tile.tile_x) || !reader.read_i64(tile.tile_z) ||
				!reader.read_u16(plane_count) || !reader.read_u16(tile_reserved) ||
				!reader.read_u32(record_bytes) || plane_count == 0 || tile_reserved != 0 ||
				record_bytes > reader.remaining())
				return fail(out_error, "Vegetation Layer tile header is invalid.");
			const size_t record_begin = reader.offset();
			uint64_t plane_charge = 0;
			if (!checked_mul(plane_count, 1041, plane_charge) ||
				!checked_add(cost.decoded_bytes, 16, cost.decoded_bytes) ||
				!checked_add(cost.decoded_bytes, plane_charge, cost.decoded_bytes) ||
				cost.decoded_bytes > budget.max_decoded_bytes)
				return fail(out_error, "Vegetation Layer decoded budget exceeded.");
			tile.planes.reserve(plane_count);
			for (uint16_t plane_index = 0; plane_index < plane_count; ++plane_index)
			{
				uint8_t kind_value = 0, codec_value = 0;
				uint16_t plane_reserved = 0;
				uint32_t decoded_bytes = 0, encoded_bytes = 0, decoded_crc = 0;
				VegetationLayerPlane plane{};
				std::vector<uint8_t> encoded{};
				if (!reader.read_u8(kind_value) || !reader.read_u8(codec_value) ||
					!reader.read_u16(plane_reserved) || !reader.read_array(plane.species_id) ||
					!reader.read_u32(decoded_bytes) || !reader.read_u32(encoded_bytes) ||
					!reader.read_u32(decoded_crc) || plane_reserved != 0 || decoded_bytes != 1024 ||
					encoded_bytes == 0 || encoded_bytes > 3072 || !reader.read_bytes(encoded_bytes, encoded))
					return fail(out_error, "Vegetation Layer plane header is invalid.");
				plane.kind = static_cast<VegetationLayerPlaneKind>(kind_value);
				const auto codec = static_cast<VegetationLayerPlaneCodec>(codec_value);
				if (!decode_plane_bytes(codec, encoded, plane.values, out_error) ||
					vegetation_crc32(plane.values.data(), plane.values.size()) != decoded_crc)
					return fail(out_error, "Vegetation Layer plane CRC is invalid.");
				tile.planes.push_back(std::move(plane));
			}
			if (reader.offset() - record_begin != record_bytes ||
				!validate_plane_sequence(tile, layer.palette, out_error) ||
				(!layer.tiles.empty() && !tile_less(layer.tiles.back(), tile)))
				return fail(out_error, "Vegetation Layer tile record shape or ordering is invalid.");
			layer.tiles.push_back(std::move(tile));
		}
		if (!reader.at_end() || !cost_within_budget(cost, budget) ||
			!same_load_cost(preflight_cost, cost))
			return fail(out_error, "Vegetation Layer payload has a tail or exceeds budget.");
		out_layer = std::move(layer);
		if (out_cost != nullptr) *out_cost = cost;
		clear_error(out_error);
		return true;
	}
}
