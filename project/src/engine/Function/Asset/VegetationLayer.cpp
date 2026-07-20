#include "Function/Asset/VegetationLayer.h"

#include "Function/Asset/VegetationAssetCodecInternal.h"
#include "Function/Asset/VegetationBrush.h"
#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
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

		struct MutationPlaneKey
		{
			int64_t tile_x = 0;
			int64_t tile_z = 0;
			VegetationLayerPlaneKind kind = VegetationLayerPlaneKind::Density;
			VegetationId species_id{};
		};

		bool mutation_plane_key_less(const MutationPlaneKey& lhs, const MutationPlaneKey& rhs)
		{
			if (lhs.tile_z != rhs.tile_z) return lhs.tile_z < rhs.tile_z;
			if (lhs.tile_x != rhs.tile_x) return lhs.tile_x < rhs.tile_x;
			if (lhs.kind != rhs.kind)
			{
				return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
			}
			return lhs.species_id < rhs.species_id;
		}

		bool same_palette_entry(
			const VegetationPaletteEntry& lhs,
			const VegetationPaletteEntry& rhs)
		{
			return lhs.species_id == rhs.species_id &&
				lhs.species_sha256 == rhs.species_sha256 &&
				lhs.species_asset_path == rhs.species_asset_path;
		}

		bool same_palette(
			const std::vector<VegetationPaletteEntry>& lhs,
			const std::vector<VegetationPaletteEntry>& rhs)
		{
			if (lhs.size() != rhs.size()) return false;
			for (size_t index = 0; index < lhs.size(); ++index)
			{
				if (!same_palette_entry(lhs[index], rhs[index])) return false;
			}
			return true;
		}

		const VegetationLayerTile* find_mutation_tile(
			const VegetationLayerSnapshot& snapshot,
			const int64_t tile_x,
			const int64_t tile_z)
		{
			const auto iterator = std::lower_bound(
				snapshot.tiles.begin(), snapshot.tiles.end(),
				VegetationLayerTile{ tile_x, tile_z, {} }, tile_less);
			return iterator != snapshot.tiles.end() &&
				iterator->tile_x == tile_x && iterator->tile_z == tile_z
				? &*iterator
				: nullptr;
		}

		VegetationLayerTile* find_mutation_tile(
			VegetationLayerSnapshot& snapshot,
			const int64_t tile_x,
			const int64_t tile_z)
		{
			const auto iterator = std::lower_bound(
				snapshot.tiles.begin(), snapshot.tiles.end(),
				VegetationLayerTile{ tile_x, tile_z, {} }, tile_less);
			return iterator != snapshot.tiles.end() &&
				iterator->tile_x == tile_x && iterator->tile_z == tile_z
				? &*iterator
				: nullptr;
		}

		VegetationLayerTile& ensure_mutation_tile(
			VegetationLayerSnapshot& snapshot,
			const int64_t tile_x,
			const int64_t tile_z)
		{
			auto iterator = std::lower_bound(
				snapshot.tiles.begin(), snapshot.tiles.end(),
				VegetationLayerTile{ tile_x, tile_z, {} }, tile_less);
			if (iterator == snapshot.tiles.end() ||
				iterator->tile_x != tile_x || iterator->tile_z != tile_z)
			{
				VegetationLayerTile tile{};
				tile.tile_x = tile_x;
				tile.tile_z = tile_z;
				iterator = snapshot.tiles.insert(iterator, std::move(tile));
			}
			return *iterator;
		}

		bool mutation_plane_less(
			const VegetationLayerPlane& lhs,
			const VegetationLayerPlane& rhs)
		{
			if (lhs.kind != rhs.kind)
			{
				return static_cast<uint8_t>(lhs.kind) < static_cast<uint8_t>(rhs.kind);
			}
			return lhs.species_id < rhs.species_id;
		}

		const VegetationLayerPlane* find_mutation_plane(
			const VegetationLayerSnapshot& snapshot,
			const MutationPlaneKey& key)
		{
			const VegetationLayerTile* tile = find_mutation_tile(snapshot, key.tile_x, key.tile_z);
			if (tile == nullptr) return nullptr;
			const auto iterator = std::lower_bound(
				tile->planes.begin(), tile->planes.end(),
				VegetationLayerPlane{ key.kind, key.species_id, {} }, mutation_plane_less);
			return iterator != tile->planes.end() &&
				iterator->kind == key.kind && iterator->species_id == key.species_id
				? &*iterator
				: nullptr;
		}

		VegetationLayerPlane* find_mutation_plane(
			VegetationLayerSnapshot& snapshot,
			const MutationPlaneKey& key)
		{
			VegetationLayerTile* tile = find_mutation_tile(snapshot, key.tile_x, key.tile_z);
			if (tile == nullptr) return nullptr;
			const auto iterator = std::lower_bound(
				tile->planes.begin(), tile->planes.end(),
				VegetationLayerPlane{ key.kind, key.species_id, {} }, mutation_plane_less);
			return iterator != tile->planes.end() &&
				iterator->kind == key.kind && iterator->species_id == key.species_id
				? &*iterator
				: nullptr;
		}

		VegetationLayerPlane& ensure_mutation_plane(
			VegetationLayerSnapshot& snapshot,
			const MutationPlaneKey& key)
		{
			VegetationLayerTile& tile = ensure_mutation_tile(snapshot, key.tile_x, key.tile_z);
			auto iterator = std::lower_bound(
				tile.planes.begin(), tile.planes.end(),
				VegetationLayerPlane{ key.kind, key.species_id, {} }, mutation_plane_less);
			if (iterator == tile.planes.end() ||
				iterator->kind != key.kind || iterator->species_id != key.species_id)
			{
				VegetationLayerPlane plane{};
				plane.kind = key.kind;
				plane.species_id = key.species_id;
				plane.values.fill(0);
				iterator = tile.planes.insert(iterator, std::move(plane));
			}
			return *iterator;
		}

		void prune_mutation_snapshot(VegetationLayerSnapshot& snapshot)
		{
			for (VegetationLayerTile& tile : snapshot.tiles)
			{
				tile.planes.erase(
					std::remove_if(
						tile.planes.begin(), tile.planes.end(),
						[](const VegetationLayerPlane& plane)
						{
							return std::all_of(
								plane.values.begin(), plane.values.end(),
								[](const uint8_t value) { return value == 0; });
						}),
						tile.planes.end());
				const bool has_density = std::any_of(
					tile.planes.begin(), tile.planes.end(),
					[](const VegetationLayerPlane& plane)
					{
						return plane.kind == VegetationLayerPlaneKind::Density;
					});
				if (!has_density)
				{
					tile.planes.clear();
				}
				std::sort(tile.planes.begin(), tile.planes.end(), mutation_plane_less);
			}
			snapshot.tiles.erase(
				std::remove_if(
					snapshot.tiles.begin(), snapshot.tiles.end(),
					[](const VegetationLayerTile& tile) { return tile.planes.empty(); }),
				snapshot.tiles.end());
			std::sort(snapshot.tiles.begin(), snapshot.tiles.end(), tile_less);
		}

		bool has_orphan_weight_tile(const VegetationLayerSnapshot& snapshot)
		{
			return std::any_of(
				snapshot.tiles.begin(), snapshot.tiles.end(),
				[](const VegetationLayerTile& tile)
				{
					const bool has_density = std::any_of(
						tile.planes.begin(), tile.planes.end(),
						[](const VegetationLayerPlane& plane)
						{
							return plane.kind == VegetationLayerPlaneKind::Density &&
								!std::all_of(
									plane.values.begin(), plane.values.end(),
									[](const uint8_t value) { return value == 0; });
						});
					const bool has_weight = std::any_of(
						tile.planes.begin(), tile.planes.end(),
						[](const VegetationLayerPlane& plane)
						{
							return plane.kind == VegetationLayerPlaneKind::SpeciesWeight &&
								!std::all_of(
									plane.values.begin(), plane.values.end(),
									[](const uint8_t value) { return value == 0; });
						});
					return !has_density && has_weight;
				});
		}

		std::vector<uint8_t> encode_mutation_plane(
			const std::array<uint8_t, 1024>& values)
		{
			if (std::all_of(values.begin(), values.end(),
				[](const uint8_t value) { return value == 0; }))
			{
				return {};
			}

			std::vector<uint8_t> bytes{};
			bytes.reserve(192);
			size_t begin = 0;
			while (begin < values.size())
			{
				size_t end = begin + 1;
				while (end < values.size() && values[end] == values[begin]) ++end;
				const uint16_t run = static_cast<uint16_t>(end - begin);
				bytes.push_back(static_cast<uint8_t>(run & 0xffu));
				bytes.push_back(static_cast<uint8_t>((run >> 8u) & 0xffu));
				bytes.push_back(values[begin]);
				begin = end;
			}
			return bytes;
		}

		bool decode_mutation_plane(
			const std::vector<uint8_t>& bytes,
			std::array<uint8_t, 1024>& out_values)
		{
			out_values.fill(0);
			if (bytes.empty()) return true;
			if ((bytes.size() % 3u) != 0) return false;

			size_t output = 0;
			uint8_t previous = 0;
			bool have_previous = false;
			for (size_t offset = 0; offset < bytes.size(); offset += 3)
			{
				const uint16_t run = static_cast<uint16_t>(bytes[offset]) |
					(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
				const uint8_t value = bytes[offset + 2];
				if (run == 0 || output + run > out_values.size() ||
					(have_previous && previous == value))
				{
					return false;
				}
				std::fill_n(out_values.begin() + static_cast<ptrdiff_t>(output), run, value);
				output += run;
				previous = value;
				have_previous = true;
			}
			return output == out_values.size() && encode_mutation_plane(out_values) == bytes;
		}

		std::vector<uint8_t> mutation_plane_bytes(
			const VegetationLayerSnapshot& snapshot,
			const MutationPlaneKey& key)
		{
			const VegetationLayerPlane* plane = find_mutation_plane(snapshot, key);
			return plane == nullptr ? std::vector<uint8_t>{} : encode_mutation_plane(plane->values);
		}

		void apply_mutation_plane_bytes(
			VegetationLayerSnapshot& snapshot,
			const MutationPlaneKey& key,
			const std::vector<uint8_t>& bytes)
		{
			if (bytes.empty())
			{
				VegetationLayerTile* tile = find_mutation_tile(snapshot, key.tile_x, key.tile_z);
				if (tile == nullptr) return;
				tile->planes.erase(
					std::remove_if(
						tile->planes.begin(), tile->planes.end(),
						[&key](const VegetationLayerPlane& plane)
						{
							return plane.kind == key.kind && plane.species_id == key.species_id;
						}),
					tile->planes.end());
				return;
			}

			std::array<uint8_t, 1024> values{};
			if (!decode_mutation_plane(bytes, values)) return;
			VegetationLayerPlane& plane = ensure_mutation_plane(snapshot, key);
			plane.values = values;
		}

		bool patch_entry_valid(const VegetationLayerPatchEntry& entry)
		{
			if (entry.plane_kind == VegetationLayerPlaneKind::Density)
			{
				if (!all_zero(entry.species_id)) return false;
			}
			else if (entry.plane_kind == VegetationLayerPlaneKind::SpeciesWeight)
			{
				if (all_zero(entry.species_id)) return false;
			}
			else
			{
				return false;
			}

			std::array<uint8_t, 1024> before{};
			std::array<uint8_t, 1024> after{};
			return decode_mutation_plane(entry.before_bytes, before) &&
				decode_mutation_plane(entry.after_bytes, after) &&
				entry.before_bytes != entry.after_bytes;
		}

		MutationPlaneKey patch_entry_key(const VegetationLayerPatchEntry& entry)
		{
			return { entry.tile_x, entry.tile_z, entry.plane_kind, entry.species_id };
		}

		bool validate_patch_shape(const VegetationLayerPatch& patch)
		{
			if (patch.entries.empty() && !patch.has_palette_change) return false;
			if (patch.has_palette_change)
			{
				std::string error{};
				if (same_palette(patch.before_palette, patch.after_palette) ||
					!validate_palette(patch.before_palette, true, &error) ||
					!validate_palette(patch.after_palette, true, &error))
				{
					return false;
				}
			}
			else if (!patch.before_palette.empty() || !patch.after_palette.empty())
			{
				return false;
			}

			for (size_t index = 0; index < patch.entries.size(); ++index)
			{
				if (!patch_entry_valid(patch.entries[index])) return false;
				if (index != 0 &&
					!mutation_plane_key_less(
						patch_entry_key(patch.entries[index - 1]),
						patch_entry_key(patch.entries[index])))
				{
					return false;
				}
			}
			return true;
		}

		VegetationLayerPatch build_mutation_patch(
			const VegetationLayerSnapshot& before,
			const VegetationLayerSnapshot& after,
			const std::set<MutationPlaneKey, decltype(&mutation_plane_key_less)>& touched,
			const bool palette_changed)
		{
			VegetationLayerPatch patch{};
			patch.has_palette_change = palette_changed;
			if (palette_changed)
			{
				patch.before_palette = before.palette;
				patch.after_palette = after.palette;
			}
			for (const MutationPlaneKey& key : touched)
			{
				VegetationLayerPatchEntry entry{};
				entry.tile_x = key.tile_x;
				entry.tile_z = key.tile_z;
				entry.plane_kind = key.kind;
				entry.species_id = key.species_id;
				entry.before_bytes = mutation_plane_bytes(before, key);
				entry.after_bytes = mutation_plane_bytes(after, key);
				if (entry.before_bytes != entry.after_bytes)
				{
					patch.entries.push_back(std::move(entry));
				}
			}
			return patch;
		}

		int64_t floor_divide(const int64_t value, const int64_t divisor)
		{
			int64_t quotient = value / divisor;
			const int64_t remainder = value % divisor;
			if (remainder < 0) --quotient;
			return quotient;
		}

		VegetationChunkCoord mutation_chunk_coord(
			const int64_t tile_x,
			const int64_t tile_z)
		{
			return { floor_divide(tile_x, 8), floor_divide(tile_z, 8) };
		}

		bool chunk_coord_less(const VegetationChunkCoord& lhs, const VegetationChunkCoord& rhs)
		{
			return lhs.z < rhs.z || (lhs.z == rhs.z && lhs.x < rhs.x);
		}

		void insert_chunk_coord(
			std::vector<VegetationChunkCoord>& coords,
			const VegetationChunkCoord coord)
		{
			const auto iterator = std::lower_bound(coords.begin(), coords.end(), coord, chunk_coord_less);
			if (iterator == coords.end() || iterator->x != coord.x || iterator->z != coord.z)
			{
				coords.insert(iterator, coord);
			}
		}

		VegetationAuthoringSpeciesDirtyEvidence& ensure_species_evidence(
			VegetationAuthoringDirtyEvidence& evidence,
			const VegetationId& species_id)
		{
			auto iterator = std::lower_bound(
				evidence.species_coords.begin(), evidence.species_coords.end(), species_id,
				[](const VegetationAuthoringSpeciesDirtyEvidence& entry, const VegetationId& value)
				{
					return entry.species_id < value;
				});
			if (iterator == evidence.species_coords.end() || iterator->species_id != species_id)
			{
				VegetationAuthoringSpeciesDirtyEvidence entry{};
				entry.species_id = species_id;
				iterator = evidence.species_coords.insert(iterator, std::move(entry));
			}
			return *iterator;
		}

		const VegetationPaletteEntry* find_palette_entry(
			const std::vector<VegetationPaletteEntry>& palette,
			const VegetationId& species_id)
		{
			const auto iterator = std::lower_bound(
				palette.begin(), palette.end(), species_id,
				[](const VegetationPaletteEntry& entry, const VegetationId& id)
				{
					return entry.species_id < id;
				});
			return iterator != palette.end() && iterator->species_id == species_id
				? &*iterator
				: nullptr;
		}

		void insert_nonzero_species_coords(
			std::vector<VegetationChunkCoord>& coords,
			const VegetationLayerSnapshot& snapshot,
			const VegetationId& species_id)
		{
			for (const VegetationLayerTile& tile : snapshot.tiles)
			{
				const bool has_nonzero_weight = std::any_of(
					tile.planes.begin(), tile.planes.end(),
					[&species_id](const VegetationLayerPlane& plane)
					{
						return plane.kind == VegetationLayerPlaneKind::SpeciesWeight &&
							plane.species_id == species_id &&
							std::any_of(
								plane.values.begin(), plane.values.end(),
								[](const uint8_t value) { return value != 0; });
					});
				if (has_nonzero_weight)
				{
					insert_chunk_coord(coords, mutation_chunk_coord(tile.tile_x, tile.tile_z));
				}
			}
		}

		void merge_patch_dirty_evidence(
			VegetationAuthoringDirtyEvidence& evidence,
			const VegetationLayerPatch& patch,
			const VegetationLayerSnapshot& source_snapshot,
			const VegetationLayerSnapshot& target_snapshot,
			const bool reverse)
		{
			for (const VegetationLayerPatchEntry& entry : patch.entries)
			{
				const VegetationChunkCoord coord = mutation_chunk_coord(entry.tile_x, entry.tile_z);
				if (entry.plane_kind == VegetationLayerPlaneKind::Density)
				{
					insert_chunk_coord(evidence.density_coords, coord);
					continue;
				}

				const std::vector<uint8_t>& source = reverse ? entry.after_bytes : entry.before_bytes;
				const std::vector<uint8_t>& target = reverse ? entry.before_bytes : entry.after_bytes;
				VegetationAuthoringSpeciesDirtyEvidence& species =
					ensure_species_evidence(evidence, entry.species_id);
				if (!source.empty()) insert_chunk_coord(species.before_coords, coord);
				if (!target.empty()) insert_chunk_coord(species.after_coords, coord);
			}

			if (!patch.has_palette_change) return;
			std::vector<VegetationId> changed_species{};
			changed_species.reserve(
				patch.before_palette.size() + patch.after_palette.size());
			for (const VegetationPaletteEntry& entry : patch.before_palette)
			{
				const VegetationPaletteEntry* other = find_palette_entry(
					patch.after_palette, entry.species_id);
				if (other == nullptr || !same_palette_entry(entry, *other))
				{
					changed_species.push_back(entry.species_id);
				}
			}
			for (const VegetationPaletteEntry& entry : patch.after_palette)
			{
				const VegetationPaletteEntry* other = find_palette_entry(
					patch.before_palette, entry.species_id);
				if (other == nullptr)
				{
					changed_species.push_back(entry.species_id);
				}
			}
			std::sort(changed_species.begin(), changed_species.end());
			changed_species.erase(
				std::unique(changed_species.begin(), changed_species.end()),
				changed_species.end());
			for (const VegetationId& species_id : changed_species)
			{
				std::vector<VegetationChunkCoord> before_coords{};
				std::vector<VegetationChunkCoord> after_coords{};
				insert_nonzero_species_coords(
					before_coords, source_snapshot, species_id);
				insert_nonzero_species_coords(
					after_coords, target_snapshot, species_id);
				VegetationAuthoringSpeciesDirtyEvidence& species =
					ensure_species_evidence(evidence, species_id);
				for (const VegetationChunkCoord coord : before_coords)
					insert_chunk_coord(species.before_coords, coord);
				for (const VegetationChunkCoord coord : after_coords)
					insert_chunk_coord(species.after_coords, coord);
			}
		}

		bool snapshot_is_canonical(const VegetationLayerSnapshot& snapshot)
		{
			std::vector<uint8_t> bytes{};
			std::string error{};
			return encode_vegetation_layer(snapshot, bytes, &error);
		}

		VegetationPatchApplyStatus apply_patch_direction(
			std::shared_ptr<const VegetationLayerSnapshot>& publication,
			const VegetationLayerMutationAccess access,
			VegetationAuthoringDirtyEvidence& dirty_evidence,
			const VegetationLayerPatch& patch,
			const uint64_t expected_current_generation,
			const bool reverse)
		{
			if (!publication || publication->content_generation != expected_current_generation)
				return VegetationPatchApplyStatus::GenerationMismatch;
			if (access != VegetationLayerMutationAccess::Editable)
				return VegetationPatchApplyStatus::ReadOnly;
			if (publication->content_generation == std::numeric_limits<uint64_t>::max())
				return VegetationPatchApplyStatus::GenerationExhausted;
			if (!snapshot_is_canonical(*publication))
				return VegetationPatchApplyStatus::InvalidPatch;
			if (!validate_patch_shape(patch))
				return VegetationPatchApplyStatus::InvalidPatch;

			const std::vector<VegetationPaletteEntry>& source_palette = reverse
				? patch.after_palette
				: patch.before_palette;
			const std::vector<VegetationPaletteEntry>& target_palette = reverse
				? patch.before_palette
				: patch.after_palette;
			if (patch.has_palette_change && !same_palette(publication->palette, source_palette))
				return VegetationPatchApplyStatus::SourceMismatch;

			for (const VegetationLayerPatchEntry& entry : patch.entries)
			{
				const MutationPlaneKey key = patch_entry_key(entry);
				const std::vector<uint8_t>& source = reverse ? entry.after_bytes : entry.before_bytes;
				if (mutation_plane_bytes(*publication, key) != source)
					return VegetationPatchApplyStatus::SourceMismatch;
			}

			VegetationLayerSnapshot candidate = *publication;
			if (patch.has_palette_change) candidate.palette = target_palette;
			for (const VegetationLayerPatchEntry& entry : patch.entries)
			{
				const std::vector<uint8_t>& target = reverse ? entry.before_bytes : entry.after_bytes;
				apply_mutation_plane_bytes(candidate, patch_entry_key(entry), target);
			}
			if (has_orphan_weight_tile(candidate))
				return VegetationPatchApplyStatus::InvalidPatch;
			prune_mutation_snapshot(candidate);
			candidate.content_generation = publication->content_generation + 1;
			if (!snapshot_is_canonical(candidate))
				return VegetationPatchApplyStatus::InvalidPatch;

			VegetationAuthoringDirtyEvidence next_evidence = dirty_evidence;
			merge_patch_dirty_evidence(
				next_evidence, patch, *publication, candidate, reverse);
			next_evidence.generation = candidate.content_generation;
			auto next_publication = std::make_shared<const VegetationLayerSnapshot>(std::move(candidate));
			publication = std::move(next_publication);
			dirty_evidence = std::move(next_evidence);
			return VegetationPatchApplyStatus::Applied;
		}

		uint64_t integer_square_root(const uint64_t value)
		{
			uint64_t result = 0;
			uint64_t bit = uint64_t{ 1 } << 62u;
			while (bit > value) bit >>= 2u;
			uint64_t remainder = value;
			while (bit != 0)
			{
				if (remainder >= result + bit)
				{
					remainder -= result + bit;
					result = (result >> 1u) + bit;
				}
				else
				{
					result >>= 1u;
				}
				bit >>= 2u;
			}
			return result;
		}

		bool checked_add_i64(const int64_t lhs, const int64_t rhs, int64_t& out_value)
		{
			if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
				(rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
			{
				return false;
			}
			out_value = lhs + rhs;
			return true;
		}

		bool checked_subtract_i64(const int64_t lhs, const int64_t rhs, int64_t& out_value)
		{
			if ((rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs) ||
				(rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs))
			{
				return false;
			}
			out_value = lhs - rhs;
			return true;
		}

		bool checked_multiply_i64_positive(
			const int64_t value,
			const int64_t multiplier,
			int64_t& out_value)
		{
			if (multiplier <= 0 ||
				(value > 0 && value > std::numeric_limits<int64_t>::max() / multiplier) ||
				(value < 0 && value < std::numeric_limits<int64_t>::min() / multiplier))
			{
				return false;
			}
			out_value = value * multiplier;
			return true;
		}

		bool world_cell_bounds(
			const int64_t center,
			const uint32_t radius,
			int64_t& out_min,
			int64_t& out_max)
		{
			int64_t minimum_numerator = 0;
			int64_t maximum_numerator = 0;
			if (!checked_add_i64(
					center, -static_cast<int64_t>(radius) - 500, minimum_numerator) ||
				!checked_add_i64(
					center, static_cast<int64_t>(radius) - 500, maximum_numerator))
			{
				return false;
			}
			out_min = floor_divide(minimum_numerator, 1000);
			out_max = floor_divide(maximum_numerator, 1000);
			return true;
		}

		bool world_cell_center_delta(
			const int64_t cell,
			const int64_t center,
			int64_t& out_delta)
		{
			int64_t scaled = 0;
			int64_t cell_center = 0;
			return checked_multiply_i64_positive(cell, 1000, scaled) &&
				checked_add_i64(scaled, 500, cell_center) &&
				checked_subtract_i64(cell_center, center, out_delta);
		}

		uint8_t saturating_add_u8(const uint8_t value, const uint8_t amount)
		{
			const uint16_t sum = static_cast<uint16_t>(value) + amount;
			return static_cast<uint8_t>(std::min<uint16_t>(sum, 255));
		}

		uint8_t saturating_sub_u8(const uint8_t value, const uint8_t amount)
		{
			return value > amount ? static_cast<uint8_t>(value - amount) : 0;
		}
	}

	struct VegetationLayerMutationInternals
	{
		static std::shared_ptr<const VegetationLayerSnapshot>& snapshot(
			VegetationLayerWorkingSet& working_set)
		{
			return working_set.m_snapshot;
		}

		static VegetationLayerMutationAccess access(
			const VegetationLayerWorkingSet& working_set)
		{
			return working_set.m_access;
		}

		static VegetationAuthoringDirtyEvidence& dirty_evidence(
			VegetationLayerWorkingSet& working_set)
		{
			return working_set.m_dirty_evidence;
		}
	};

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

	VegetationLayerWorkingSet::VegetationLayerWorkingSet(
		std::shared_ptr<const VegetationLayerSnapshot> snapshot,
		const VegetationLayerMutationAccess access)
		: m_snapshot(std::move(snapshot)), m_access(access)
	{
		m_dirty_evidence.generation = content_generation();
	}

	std::shared_ptr<const VegetationLayerSnapshot>
	VegetationLayerWorkingSet::publish_snapshot() const
	{
		return m_snapshot;
	}

	VegetationAuthoringDirtyEvidence
	VegetationLayerWorkingSet::snapshot_bake_dirty_evidence() const
	{
		VegetationAuthoringDirtyEvidence evidence = m_dirty_evidence;
		evidence.generation = content_generation();
		return evidence;
	}

	bool VegetationLayerWorkingSet::acknowledge_bake_dirty_evidence(
		const uint64_t captured_generation)
	{
		if (!m_snapshot || captured_generation != m_snapshot->content_generation)
		{
			return false;
		}
		m_dirty_evidence = {};
		m_dirty_evidence.generation = captured_generation;
		return true;
	}

	uint64_t VegetationLayerWorkingSet::content_generation() const
	{
		return m_snapshot ? m_snapshot->content_generation : 0;
	}

	VegetationPatchApplyStatus apply_vegetation_layer_patch(
		VegetationLayerWorkingSet& working_set,
		const VegetationLayerPatch& patch,
		const uint64_t expected_current_generation)
	{
		return apply_patch_direction(
			VegetationLayerMutationInternals::snapshot(working_set),
			VegetationLayerMutationInternals::access(working_set),
			VegetationLayerMutationInternals::dirty_evidence(working_set),
			patch,
			expected_current_generation,
			false);
	}

	VegetationPatchApplyStatus revert_vegetation_layer_patch(
		VegetationLayerWorkingSet& working_set,
		const VegetationLayerPatch& patch,
		const uint64_t expected_current_generation)
	{
		return apply_patch_direction(
			VegetationLayerMutationInternals::snapshot(working_set),
			VegetationLayerMutationInternals::access(working_set),
			VegetationLayerMutationInternals::dirty_evidence(working_set),
			patch,
			expected_current_generation,
			true);
	}

	VegetationPaletteApplyResult apply_vegetation_palette_edit(
		VegetationLayerWorkingSet& working_set,
		const VegetationPaletteEdit& edit)
	{
		VegetationPaletteApplyResult result{};
		result.new_generation = working_set.content_generation();
		const auto publication = VegetationLayerMutationInternals::snapshot(working_set);
		if (!publication ||
			VegetationLayerMutationInternals::access(working_set) !=
				VegetationLayerMutationAccess::Editable ||
			working_set.content_generation() == std::numeric_limits<uint64_t>::max() ||
			!snapshot_is_canonical(*publication))
		{
			return result;
		}

		const VegetationLayerSnapshot& before = *publication;
		VegetationLayerSnapshot after = before;
		std::set<MutationPlaneKey, decltype(&mutation_plane_key_less)> touched(
			&mutation_plane_key_less);
		bool valid = false;

		switch (edit.mode)
		{
		case VegetationPaletteEditMode::Add:
		{
			if (all_zero(edit.replacement.species_id) ||
				std::any_of(
					before.palette.begin(), before.palette.end(),
					[&edit](const VegetationPaletteEntry& entry)
					{
						return entry.species_id == edit.replacement.species_id ||
							entry.species_asset_path == edit.replacement.species_asset_path;
					}))
			{
				break;
			}
			after.palette.push_back(edit.replacement);
			std::sort(
				after.palette.begin(), after.palette.end(),
				[](const VegetationPaletteEntry& lhs, const VegetationPaletteEntry& rhs)
				{
					return lhs.species_id < rhs.species_id;
				});
			valid = true;
			break;
		}
		case VegetationPaletteEditMode::Replace:
		{
			auto target = std::lower_bound(
				after.palette.begin(), after.palette.end(), edit.target_species_id,
				[](const VegetationPaletteEntry& entry, const VegetationId& id)
				{
					return entry.species_id < id;
				});
			if (target == after.palette.end() || target->species_id != edit.target_species_id ||
				edit.replacement.species_id != edit.target_species_id ||
				edit.replacement.species_asset_path == target->species_asset_path ||
				edit.replacement.species_sha256 == target->species_sha256 ||
				std::any_of(
					after.palette.begin(), after.palette.end(),
					[&target, &edit](const VegetationPaletteEntry& entry)
					{
						return &entry != &*target &&
							entry.species_asset_path == edit.replacement.species_asset_path;
					}))
			{
				break;
			}
			*target = edit.replacement;
			valid = true;
			break;
		}
		case VegetationPaletteEditMode::Remove:
		{
			const auto target = std::lower_bound(
				after.palette.begin(), after.palette.end(), edit.target_species_id,
				[](const VegetationPaletteEntry& entry, const VegetationId& id)
				{
					return entry.species_id < id;
				});
			if (target == after.palette.end() || target->species_id != edit.target_species_id)
			{
				break;
			}

			bool has_weights = false;
			for (const VegetationLayerTile& tile : before.tiles)
			{
				for (const VegetationLayerPlane& plane : tile.planes)
				{
					if (plane.kind == VegetationLayerPlaneKind::SpeciesWeight &&
						plane.species_id == edit.target_species_id)
					{
						has_weights = true;
						touched.insert({
							tile.tile_x, tile.tile_z,
							VegetationLayerPlaneKind::SpeciesWeight,
							edit.target_species_id });
					}
				}
			}
			if (has_weights && !edit.clear_weights) break;

			if (edit.clear_weights)
			{
				for (VegetationLayerTile& tile : after.tiles)
				{
					tile.planes.erase(
						std::remove_if(
							tile.planes.begin(), tile.planes.end(),
							[&edit](const VegetationLayerPlane& plane)
							{
								return plane.kind == VegetationLayerPlaneKind::SpeciesWeight &&
									plane.species_id == edit.target_species_id;
							}),
						tile.planes.end());
				}
			}
			after.palette.erase(std::lower_bound(
				after.palette.begin(), after.palette.end(), edit.target_species_id,
				[](const VegetationPaletteEntry& entry, const VegetationId& id)
				{
					return entry.species_id < id;
				}));
			valid = true;
			break;
		}
		default:
			break;
		}

		if (!valid) return result;
		prune_mutation_snapshot(after);
		after.content_generation = before.content_generation + 1;
		if (!snapshot_is_canonical(after)) return result;

		VegetationLayerPatch patch = build_mutation_patch(before, after, touched, true);
		const VegetationPatchApplyStatus status = apply_vegetation_layer_patch(
			working_set, patch, before.content_generation);
		if (status != VegetationPatchApplyStatus::Applied) return result;

		result.status = VegetationMutationStatus::Applied;
		result.patch = std::move(patch);
		result.new_generation = working_set.content_generation();
		return result;
	}

	VegetationBrushApplyResult apply_vegetation_brush_stroke(
		VegetationLayerWorkingSet& working_set,
		const VegetationBrushStroke& stroke)
	{
		VegetationBrushApplyResult result{};
		result.new_generation = working_set.content_generation();
		const auto publication = VegetationLayerMutationInternals::snapshot(working_set);
		if (!publication ||
			VegetationLayerMutationInternals::access(working_set) !=
				VegetationLayerMutationAccess::Editable ||
			working_set.content_generation() == std::numeric_limits<uint64_t>::max() ||
			!snapshot_is_canonical(*publication) ||
			stroke.path.empty() || stroke.radius_mm < 250 || stroke.radius_mm > 1024000 ||
			stroke.strength == 0 || stroke.spacing_mm == 0 || stroke.spacing_mm > 2048000 ||
			(stroke.mode != VegetationBrushMode::Paint && stroke.mode != VegetationBrushMode::Erase) ||
			(stroke.mode == VegetationBrushMode::Paint &&
				(all_zero(stroke.selected_species) ||
					!contains_id(publication->palette, stroke.selected_species))))
		{
			return result;
		}

		std::vector<VegetationWorldMillimeterPoint> raw_points{};
		raw_points.reserve(stroke.path.size());
		for (const VegetationSurfaceSampleRequest& request : stroke.path)
		{
			VegetationWorldMillimeterPoint point{};
			if (!vegetation_surface_request_to_world_millimeter(request, point)) return result;
			raw_points.push_back(point);
		}
		const VegetationStrokeResampleResult resampled =
			resample_vegetation_stroke(raw_points, stroke.spacing_mm);
		if (!resampled.succeeded || resampled.dabs.empty()) return result;

		const VegetationLayerSnapshot& before = *publication;
		VegetationLayerSnapshot after = before;
		std::set<MutationPlaneKey, decltype(&mutation_plane_key_less)> touched(
			&mutation_plane_key_less);

		for (const VegetationWorldMillimeterPoint& dab : resampled.dabs)
		{
			int64_t min_cell_x = 0;
			int64_t max_cell_x = 0;
			int64_t min_cell_z = 0;
			int64_t max_cell_z = 0;
			if (!world_cell_bounds(dab.x, stroke.radius_mm, min_cell_x, max_cell_x) ||
				!world_cell_bounds(dab.z, stroke.radius_mm, min_cell_z, max_cell_z))
			{
				return result;
			}

			for (int64_t cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z)
			{
				int64_t delta_z = 0;
				if (!world_cell_center_delta(cell_z, dab.z, delta_z)) return result;
				const uint64_t absolute_z = static_cast<uint64_t>(delta_z < 0 ? -delta_z : delta_z);
				for (int64_t cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x)
				{
					int64_t delta_x = 0;
					if (!world_cell_center_delta(cell_x, dab.x, delta_x)) return result;
					const uint64_t absolute_x = static_cast<uint64_t>(delta_x < 0 ? -delta_x : delta_x);
					const uint64_t square_distance =
						absolute_x * absolute_x + absolute_z * absolute_z;
					const uint8_t amount = vegetation_brush_amount(
						integer_square_root(square_distance),
						stroke.radius_mm,
						stroke.strength,
						stroke.falloff);
					if (amount == 0) continue;

					const int64_t tile_x = floor_divide(cell_x, 32);
					const int64_t tile_z = floor_divide(cell_z, 32);
					const size_t local_x = static_cast<size_t>(cell_x - tile_x * 32);
					const size_t local_z = static_cast<size_t>(cell_z - tile_z * 32);
					const size_t texel_index = local_z * 32 + local_x;
					const MutationPlaneKey density_key{
						tile_x, tile_z, VegetationLayerPlaneKind::Density, {} };

					if (stroke.mode == VegetationBrushMode::Paint)
					{
						touched.insert(density_key);
						VegetationLayerPlane& density = ensure_mutation_plane(after, density_key);
						density.values[texel_index] =
							saturating_add_u8(density.values[texel_index], amount);

						const MutationPlaneKey weight_key{
							tile_x, tile_z, VegetationLayerPlaneKind::SpeciesWeight,
							stroke.selected_species };
						touched.insert(weight_key);
						VegetationLayerPlane& weight = ensure_mutation_plane(after, weight_key);
						weight.values[texel_index] =
							saturating_add_u8(weight.values[texel_index], amount);
					}
					else
					{
						VegetationLayerTile* tile = find_mutation_tile(after, tile_x, tile_z);
						if (tile == nullptr) continue;
						for (VegetationLayerPlane& plane : tile->planes)
						{
							MutationPlaneKey key{ tile_x, tile_z, plane.kind, plane.species_id };
							touched.insert(key);
							plane.values[texel_index] =
								saturating_sub_u8(plane.values[texel_index], amount);
						}
					}
				}
			}
		}

		prune_mutation_snapshot(after);
		VegetationLayerPatch patch = build_mutation_patch(before, after, touched, false);
		if (patch.entries.empty())
		{
			result.status = VegetationMutationStatus::NoChange;
			return result;
		}

		const VegetationPatchApplyStatus status = apply_vegetation_layer_patch(
			working_set, patch, before.content_generation);
		if (status != VegetationPatchApplyStatus::Applied) return result;
		result.status = VegetationMutationStatus::Applied;
		result.patch = std::move(patch);
		result.new_generation = working_set.content_generation();
		return result;
	}
}
