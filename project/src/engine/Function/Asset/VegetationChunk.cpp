#include "Function/Asset/VegetationChunk.h"

#include "Function/Asset/VegetationAssetCodecInternal.h"
#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace AshEngine
{
	namespace
	{
		using namespace VegetationAssetCodecInternal;
		constexpr size_t k_header_size = 160;
		constexpr std::array<uint8_t, 4> k_magic{ 'A', 'S', 'V', 'C' };

		bool instance_less(
			const VegetationChunk& chunk,
			const VegetationChunkInstance& lhs,
			const VegetationChunkInstance& rhs)
		{
			const VegetationId& lhs_id = chunk.species[lhs.species_index].species_id;
			const VegetationId& rhs_id = chunk.species[rhs.species_index].species_id;
			if (lhs_id != rhs_id) return lhs_id < rhs_id;
			if (lhs.cell_z != rhs.cell_z) return lhs.cell_z < rhs.cell_z;
			if (lhs.cell_x != rhs.cell_x) return lhs.cell_x < rhs.cell_x;
			return lhs.candidate_ordinal < rhs.candidate_ordinal;
		}

		bool validate_chunk(const VegetationChunk& chunk, std::string* out_error)
		{
			if (chunk.cooker_version != 1 || all_zero(chunk.layer_id) ||
				all_zero(chunk.chunk_input_sha256) || all_zero(chunk.surface_identity.surface_id) ||
				!validate_palette(chunk.species, false, out_error) || chunk.instances.empty() ||
				chunk.instances.size() > std::numeric_limits<uint32_t>::max() ||
				chunk.min_world_height_mm > chunk.max_world_height_mm)
				return fail(out_error, "Vegetation Chunk identity, counts, or extrema are invalid.");
			std::vector<bool> referenced(chunk.species.size(), false);
			int32_t actual_min = std::numeric_limits<int32_t>::max();
			int32_t actual_max = std::numeric_limits<int32_t>::min();
			for (size_t index = 0; index < chunk.instances.size(); ++index)
			{
				const VegetationChunkInstance& instance = chunk.instances[index];
				if (instance.species_index >= chunk.species.size() || instance.cell_x > 255 ||
					instance.cell_z > 255 || instance.candidate_ordinal > 255 || instance.scale_q12 == 0 ||
					instance.normal_oct_x == std::numeric_limits<int16_t>::min() ||
					instance.normal_oct_y == std::numeric_limits<int16_t>::min() ||
					instance.world_height_mm < chunk.min_world_height_mm ||
					instance.world_height_mm > chunk.max_world_height_mm ||
					(index != 0 && !instance_less(chunk, chunk.instances[index - 1], instance)))
					return fail(out_error, "Vegetation Chunk instance is invalid or unsorted.");
				referenced[instance.species_index] = true;
				actual_min = std::min(actual_min, instance.world_height_mm);
				actual_max = std::max(actual_max, instance.world_height_mm);
			}
			if (actual_min != chunk.min_world_height_mm || actual_max != chunk.max_world_height_mm ||
				std::find(referenced.begin(), referenced.end(), false) != referenced.end())
				return fail(out_error, "Vegetation Chunk extrema or species references are incomplete.");
			return true;
		}

		bool add_chunk_palette_cost(VegetationLoadCost& cost, const VegetationPaletteEntry& entry)
		{
			uint64_t charge = 0;
			return checked_add(48, entry.species_asset_path.size(), charge) &&
				checked_add(cost.decoded_bytes, charge, cost.decoded_bytes);
		}

		bool preflight_chunk(
			const std::vector<uint8_t>& bytes,
			const VegetationLoadBudget& budget,
			VegetationLoadCost& cost,
			std::string* out_error)
		{
			cost = {};
			if (bytes.size() < k_header_size)
				return fail(out_error, "Vegetation Chunk file is smaller than its header.");
			if (bytes.size() > budget.max_file_bytes)
			{
				cost.file_bytes = bytes.size();
				return fail(out_error, "Vegetation Chunk file budget exceeded before ownership.");
			}
			ByteReader header(bytes);
			std::array<uint8_t, 4> magic{};
			uint16_t version = 0, header_size = 0;
			uint32_t cooker = 0, flags = 0, species_count = 0, instance_count = 0;
			int32_t min_height = 0, max_height = 0;
			uint64_t payload_bytes = 0, reserved = 0;
			uint32_t payload_crc = 0, header_crc = 0;
			VegetationId layer_id{}, surface_id{};
			VegetationSha256 input_sha{};
			int64_t chunk_x = 0, chunk_z = 0;
			uint64_t content_revision = 0, residency_revision = 0, transform_revision = 0;
			if (!header.read_array(magic) || !header.read_u16(version) || !header.read_u16(header_size) ||
				!header.read_u32(cooker) || !header.read_u32(flags) || !header.read_array(layer_id) ||
				!header.read_array(input_sha) || !header.read_i64(chunk_x) || !header.read_i64(chunk_z) ||
				!header.read_array(surface_id) || !header.read_u64(content_revision) ||
				!header.read_u64(residency_revision) || !header.read_u64(transform_revision) ||
				!header.read_u32(species_count) || !header.read_u32(instance_count) ||
				!header.read_i32(min_height) || !header.read_i32(max_height) ||
				!header.read_u64(payload_bytes) || !header.read_u32(payload_crc) ||
				!header.read_u32(header_crc) || !header.read_u64(reserved) ||
				magic != k_magic || version != 1 || header_size != k_header_size || cooker != 1 || flags != 0 ||
				all_zero(layer_id) || all_zero(input_sha) || all_zero(surface_id) ||
				species_count == 0 || species_count > 65534 || instance_count == 0 ||
				min_height > max_height || reserved != 0 ||
				payload_bytes != bytes.size() - k_header_size)
				return fail(out_error, "Vegetation Chunk header is invalid.");
			cost.file_bytes = bytes.size();
			cost.payload_bytes = payload_bytes;
			cost.decoded_bytes = 112;
			cost.palette_records = species_count;
			cost.instance_records = instance_count;
			if (payload_bytes > budget.max_payload_bytes ||
				species_count > budget.max_palette_records || instance_count > budget.max_instance_records)
				return fail(out_error, "Vegetation Chunk count or payload budget exceeded before ownership.");
			if (!header_crc_matches<k_header_size>(bytes, 148, header_crc) ||
				vegetation_crc32(bytes.data() + k_header_size, bytes.size() - k_header_size) != payload_crc)
				return fail(out_error, "Vegetation Chunk CRC is invalid.");
			uint64_t minimum_species = 0, instance_bytes = 0, minimum_payload = 0;
			if (!checked_mul(species_count, 53, minimum_species) ||
				!checked_mul(instance_count, 28, instance_bytes) ||
				!checked_add(minimum_species, instance_bytes, minimum_payload) || minimum_payload > payload_bytes ||
				!checked_add(cost.decoded_bytes, instance_bytes, cost.decoded_bytes))
				return fail(out_error, "Vegetation Chunk declared counts or decoded cost are invalid.");

			ByteReader reader(bytes, k_header_size);
			if (!scan_palette_preflight(bytes, reader, species_count, false, cost, out_error)) return false;
			std::array<uint64_t, 1024> referenced{};
			uint16_t previous_species = 0, previous_cell_x = 0, previous_cell_z = 0, previous_ordinal = 0;
			bool have_previous = false;
			int32_t actual_min = std::numeric_limits<int32_t>::max();
			int32_t actual_max = std::numeric_limits<int32_t>::min();
			for (uint32_t index = 0; index < instance_count; ++index)
			{
				uint16_t species_index = 0, cell_x = 0, cell_z = 0, ordinal = 0;
				uint16_t fraction_x = 0, fraction_z = 0, yaw = 0, scale = 0;
				int16_t oct_x = 0, oct_y = 0;
				int32_t height = 0;
				uint32_t instance_reserved = 0;
				if (!reader.read_u16(species_index) || !reader.read_u16(cell_x) ||
					!reader.read_u16(cell_z) || !reader.read_u16(ordinal) ||
					!reader.read_u16(fraction_x) || !reader.read_u16(fraction_z) ||
					!reader.read_u16(yaw) || !reader.read_u16(scale) ||
					!reader.read_i16(oct_x) || !reader.read_i16(oct_y) ||
					!reader.read_i32(height) || !reader.read_u32(instance_reserved) ||
					species_index >= species_count || cell_x > 255 || cell_z > 255 || ordinal > 255 ||
					scale == 0 || oct_x == std::numeric_limits<int16_t>::min() ||
					oct_y == std::numeric_limits<int16_t>::min() || instance_reserved != 0 ||
					height < min_height || height > max_height)
					return fail(out_error, "Vegetation Chunk instance record is invalid.");
				const bool ordered = !have_previous || species_index > previous_species ||
					(species_index == previous_species &&
						(cell_z > previous_cell_z || (cell_z == previous_cell_z &&
							(cell_x > previous_cell_x ||
								(cell_x == previous_cell_x && ordinal > previous_ordinal)))));
				if (!ordered) return fail(out_error, "Vegetation Chunk instances are unsorted or duplicated.");
				referenced[species_index / 64u] |= uint64_t{ 1 } << (species_index % 64u);
				actual_min = std::min(actual_min, height);
				actual_max = std::max(actual_max, height);
				previous_species = species_index;
				previous_cell_x = cell_x;
				previous_cell_z = cell_z;
				previous_ordinal = ordinal;
				have_previous = true;
			}
			if (!reader.at_end() || actual_min != min_height || actual_max != max_height)
				return fail(out_error, "Vegetation Chunk payload or extrema are invalid.");
			for (uint32_t species = 0; species < species_count; ++species)
			{
				if ((referenced[species / 64u] & (uint64_t{ 1 } << (species % 64u))) == 0)
					return fail(out_error, "Vegetation Chunk contains an unreferenced species.");
			}
			if (decide_vegetation_ownership(cost, budget) == VegetationOwnershipDecision::BudgetRejected)
				return fail(out_error, "Vegetation Chunk decoded budget exceeded before ownership.");
			return true;
		}
	}

	bool encode_vegetation_chunk(
		const VegetationChunk& chunk,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error)
	{
		out_bytes.clear();
		clear_error(out_error);
		if (!validate_chunk(chunk, out_error)) return false;

		ByteWriter payload{};
		if (!write_palette(chunk.species, payload, out_error)) return false;
		for (const VegetationChunkInstance& instance : chunk.instances)
		{
			payload.write_u16(instance.species_index);
			payload.write_u16(instance.cell_x);
			payload.write_u16(instance.cell_z);
			payload.write_u16(instance.candidate_ordinal);
			payload.write_u16(instance.cell_fraction_x_u16);
			payload.write_u16(instance.cell_fraction_z_u16);
			payload.write_u16(instance.yaw_turn_u16);
			payload.write_u16(instance.scale_q12);
			payload.write_i16(instance.normal_oct_x);
			payload.write_i16(instance.normal_oct_y);
			payload.write_i32(instance.world_height_mm);
			payload.write_u32(0);
		}

		ByteWriter header{};
		header.write_array(k_magic);
		header.write_u16(1);
		header.write_u16(static_cast<uint16_t>(k_header_size));
		header.write_u32(chunk.cooker_version);
		header.write_u32(0);
		header.write_array(chunk.layer_id);
		header.write_array(chunk.chunk_input_sha256);
		header.write_i64(chunk.chunk.x);
		header.write_i64(chunk.chunk.z);
		header.write_array(chunk.surface_identity.surface_id);
		header.write_u64(chunk.surface_identity.content_revision);
		header.write_u64(chunk.surface_identity.residency_revision);
		header.write_u64(chunk.surface_identity.transform_revision);
		header.write_u32(static_cast<uint32_t>(chunk.species.size()));
		header.write_u32(static_cast<uint32_t>(chunk.instances.size()));
		header.write_i32(chunk.min_world_height_mm);
		header.write_i32(chunk.max_world_height_mm);
		header.write_u64(payload.bytes.size());
		header.write_u32(vegetation_crc32(payload.bytes.data(), payload.bytes.size()));
		header.write_u32(0);
		header.write_u64(0);
		if (header.bytes.size() != k_header_size)
			return fail(out_error, "Vegetation Chunk header construction failed.");
		write_u32_at(header.bytes, 148, vegetation_crc32(header.bytes.data(), header.bytes.size()));
		header.bytes.insert(header.bytes.end(), payload.bytes.begin(), payload.bytes.end());
		out_bytes = std::move(header.bytes);
		clear_error(out_error);
		return true;
	}

	bool decode_vegetation_chunk(
		const std::vector<uint8_t>& bytes,
		const VegetationLoadBudget& budget,
		VegetationChunk& out_chunk,
		std::string* out_error,
		VegetationLoadCost* out_cost)
	{
		out_chunk = {};
		if (out_cost != nullptr) *out_cost = {};
		clear_error(out_error);
		if (bytes.size() < k_header_size || bytes.size() > budget.max_file_bytes)
			return fail(out_error, "Vegetation Chunk file size exceeds its budget or header.");
		VegetationLoadCost preflight_cost{};
		if (!preflight_chunk(bytes, budget, preflight_cost, out_error)) return false;

		ByteReader header(bytes);
		std::array<uint8_t, 4> magic{};
		uint16_t version = 0, header_size = 0;
		uint32_t cooker = 0, flags = 0, species_count = 0, instance_count = 0;
		int32_t min_height = 0, max_height = 0;
		uint64_t payload_bytes = 0;
		uint32_t payload_crc = 0, header_crc = 0;
		uint64_t reserved = 0;
		VegetationChunk chunk{};
		if (!header.read_array(magic) || !header.read_u16(version) || !header.read_u16(header_size) ||
			!header.read_u32(cooker) || !header.read_u32(flags) || !header.read_array(chunk.layer_id) ||
			!header.read_array(chunk.chunk_input_sha256) || !header.read_i64(chunk.chunk.x) ||
			!header.read_i64(chunk.chunk.z) || !header.read_array(chunk.surface_identity.surface_id) ||
			!header.read_u64(chunk.surface_identity.content_revision) ||
			!header.read_u64(chunk.surface_identity.residency_revision) ||
			!header.read_u64(chunk.surface_identity.transform_revision) ||
			!header.read_u32(species_count) || !header.read_u32(instance_count) ||
			!header.read_i32(min_height) || !header.read_i32(max_height) ||
			!header.read_u64(payload_bytes) || !header.read_u32(payload_crc) ||
			!header.read_u32(header_crc) || !header.read_u64(reserved) ||
			magic != k_magic || version != 1 || header_size != k_header_size || cooker != 1 || flags != 0 ||
			all_zero(chunk.layer_id) || all_zero(chunk.chunk_input_sha256) ||
			all_zero(chunk.surface_identity.surface_id) || species_count == 0 || species_count > 65534 ||
			instance_count == 0 || min_height > max_height || reserved != 0 ||
			payload_bytes != bytes.size() - k_header_size || payload_bytes > budget.max_payload_bytes ||
			species_count > budget.max_palette_records || instance_count > budget.max_instance_records)
			return fail(out_error, "Vegetation Chunk header is invalid.");
		chunk.cooker_version = cooker;
		chunk.min_world_height_mm = min_height;
		chunk.max_world_height_mm = max_height;

		std::vector<uint8_t> header_copy(bytes.begin(), bytes.begin() + k_header_size);
		write_u32_at(header_copy, 148, 0);
		if (vegetation_crc32(header_copy.data(), header_copy.size()) != header_crc ||
			vegetation_crc32(bytes.data() + k_header_size, bytes.size() - k_header_size) != payload_crc)
			return fail(out_error, "Vegetation Chunk CRC is invalid.");

		uint64_t minimum_species = 0, instance_bytes = 0, minimum_payload = 0;
		if (!checked_mul(species_count, 53, minimum_species) ||
			!checked_mul(instance_count, 28, instance_bytes) ||
			!checked_add(minimum_species, instance_bytes, minimum_payload) || minimum_payload > payload_bytes)
			return fail(out_error, "Vegetation Chunk declared counts exceed its payload.");

		VegetationLoadCost cost{};
		cost.file_bytes = bytes.size();
		cost.payload_bytes = payload_bytes;
		cost.decoded_bytes = 112;
		cost.palette_records = species_count;
		cost.instance_records = instance_count;
		uint64_t instance_charge = 0;
		if (!checked_mul(instance_count, 28, instance_charge) ||
			!checked_add(cost.decoded_bytes, instance_charge, cost.decoded_bytes) ||
			cost.decoded_bytes > budget.max_decoded_bytes)
			return fail(out_error, "Vegetation Chunk decoded budget exceeded.");

		ByteReader reader(bytes, k_header_size);
		chunk.species.reserve(species_count);
		for (uint32_t index = 0; index < species_count; ++index)
		{
			VegetationPaletteEntry entry{};
			if (!read_palette_entry(reader, entry, out_error)) return false;
			if (!add_chunk_palette_cost(cost, entry) || cost.decoded_bytes > budget.max_decoded_bytes)
				return fail(out_error, "Vegetation Chunk decoded budget exceeded.");
			chunk.species.push_back(std::move(entry));
		}
		if (!validate_palette(chunk.species, false, out_error)) return false;
		chunk.instances.reserve(instance_count);
		for (uint32_t index = 0; index < instance_count; ++index)
		{
			VegetationChunkInstance instance{};
			uint32_t instance_reserved = 0;
			if (!reader.read_u16(instance.species_index) || !reader.read_u16(instance.cell_x) ||
				!reader.read_u16(instance.cell_z) || !reader.read_u16(instance.candidate_ordinal) ||
				!reader.read_u16(instance.cell_fraction_x_u16) ||
				!reader.read_u16(instance.cell_fraction_z_u16) ||
				!reader.read_u16(instance.yaw_turn_u16) || !reader.read_u16(instance.scale_q12) ||
				!reader.read_i16(instance.normal_oct_x) || !reader.read_i16(instance.normal_oct_y) ||
				!reader.read_i32(instance.world_height_mm) || !reader.read_u32(instance_reserved) ||
				instance_reserved != 0)
				return fail(out_error, "Vegetation Chunk instance record is truncated or reserved.");
			chunk.instances.push_back(instance);
		}
		if (!reader.at_end() || !cost_within_budget(cost, budget) ||
			!same_load_cost(preflight_cost, cost) || !validate_chunk(chunk, out_error))
			return fail(out_error, "Vegetation Chunk payload is invalid, tailed, or over budget.");
		out_chunk = std::move(chunk);
		if (out_cost != nullptr) *out_cost = cost;
		clear_error(out_error);
		return true;
	}
}
