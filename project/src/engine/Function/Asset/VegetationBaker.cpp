#include "Function/Asset/VegetationBaker.h"

#include "Function/Asset/VegetationAssetCodecInternal.h"
#include "Function/Asset/VegetationChunkSet.h"
#include "Function/Asset/VegetationCodec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace AshEngine
{
	namespace
	{
		using namespace VegetationAssetCodecInternal;

		bool all_zero(const VegetationId& value)
		{
			return std::all_of(value.begin(), value.end(), [](const uint8_t byte)
			{
				return byte == 0;
			});
		}

		bool operation_active(const VegetationOperationControl& control)
		{
			return control.cancel_requested != nullptr &&
				control.deadline != std::chrono::steady_clock::time_point{} &&
				!control.cancel_requested->load(std::memory_order_acquire) &&
				std::chrono::steady_clock::now() < control.deadline;
		}

		bool surface_identities_equal(
			const VegetationSurfaceIdentity& lhs,
			const VegetationSurfaceIdentity& rhs)
		{
			return lhs.surface_id == rhs.surface_id &&
				lhs.content_revision == rhs.content_revision &&
				lhs.residency_revision == rhs.residency_revision &&
				lhs.transform_revision == rhs.transform_revision;
		}

		bool digest_is_nonzero(const VegetationSha256& digest)
		{
			return std::any_of(digest.begin(), digest.end(), [](const uint8_t byte)
			{
				return byte != 0;
			});
		}

		bool chunk_coord_less(
			const VegetationChunkCoord& lhs,
			const VegetationChunkCoord& rhs)
		{
			return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
		}

		bool chunk_coord_equal(
			const VegetationChunkCoord& lhs,
			const VegetationChunkCoord& rhs)
		{
			return lhs.x == rhs.x && lhs.z == rhs.z;
		}

		int64_t floor_divide_by_eight(const int64_t value)
		{
			const int64_t quotient = value / 8;
			return value < 0 && value % 8 != 0 ? quotient - 1 : quotient;
		}

		bool validate_active_snapshot(const VegetationActiveChunkSetSnapshot& snapshot)
		{
			if (all_zero(snapshot.layer_id) || snapshot.layer_generation == 0 ||
				all_zero(snapshot.surface_identity.surface_id) ||
				!digest_is_nonzero(snapshot.manifest_sha256))
			{
				return false;
			}
			for (size_t index = 0; index < snapshot.entries.size(); ++index)
			{
				const VegetationActiveChunkSetEntrySummary& entry = snapshot.entries[index];
				if (!digest_is_nonzero(entry.object_sha256) ||
					!digest_is_nonzero(entry.input_sha256) ||
					entry.referenced_species_ids.empty() ||
					(index != 0 && !chunk_coord_less(snapshot.entries[index - 1].coord, entry.coord)))
				{
					return false;
				}
				for (size_t species = 0; species < entry.referenced_species_ids.size(); ++species)
				{
					if (all_zero(entry.referenced_species_ids[species]) ||
						(species != 0 && !(entry.referenced_species_ids[species - 1] <
							entry.referenced_species_ids[species])))
					{
						return false;
					}
				}
			}
			return true;
		}

		bool validate_source_active_identity(const VegetationBakeInput& input)
		{
			switch (input.source_active_identity.state)
			{
			case VegetationChunkSetSourceActiveState::NoActive:
				return !digest_is_nonzero(
					input.source_active_identity.manifest_sha256) &&
					input.active_chunk_set == nullptr;
			case VegetationChunkSetSourceActiveState::Existing:
				return digest_is_nonzero(
					input.source_active_identity.manifest_sha256) &&
					input.active_chunk_set != nullptr &&
					input.source_active_identity.manifest_sha256 ==
						input.active_chunk_set->manifest_sha256;
			case VegetationChunkSetSourceActiveState::Invalid:
			default:
				return false;
			}
		}

		VegetationBakeResult failed_bake(
			const VegetationOperationControl& control,
			std::string error)
		{
			VegetationBakeResult result{};
			if (control.cancel_requested != nullptr &&
				control.cancel_requested->load(std::memory_order_acquire))
			{
				result.status = VegetationBakeStatus::Cancelled;
			}
			else if (control.deadline != std::chrono::steady_clock::time_point{} &&
				std::chrono::steady_clock::now() >= control.deadline)
			{
				result.status = VegetationBakeStatus::TimedOut;
			}
			result.error = std::move(error);
			return result;
		}

		uint64_t read_u64_le(const VegetationId& id, const size_t offset)
		{
			uint64_t value = 0;
			for (size_t index = 0; index < 8; ++index)
			{
				value |= static_cast<uint64_t>(id[offset + index]) << (index * 8);
			}
			return value;
		}

		uint64_t splitmix64(uint64_t value)
		{
			uint64_t mixed = value + 0x9e3779b97f4a7c15ull;
			mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ull;
			mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebull;
			return mixed ^ (mixed >> 31);
		}

		void append_u16(std::vector<uint8_t>& bytes, const uint16_t value)
		{
			bytes.push_back(static_cast<uint8_t>(value));
			bytes.push_back(static_cast<uint8_t>(value >> 8));
		}

		void append_u32(std::vector<uint8_t>& bytes, const uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
			{
				bytes.push_back(static_cast<uint8_t>(value >> shift));
			}
		}

		void append_u64(std::vector<uint8_t>& bytes, const uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
			{
				bytes.push_back(static_cast<uint8_t>(value >> shift));
			}
		}

		uint32_t read_u32_at(const std::vector<uint8_t>& bytes, const size_t offset)
		{
			uint32_t value = 0;
			for (size_t index = 0; index < 4; ++index)
			{
				value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
			}
			return value;
		}

		int64_t read_i64_at(const std::vector<uint8_t>& bytes, const size_t offset)
		{
			uint64_t value = 0;
			for (size_t index = 0; index < 8; ++index)
			{
				value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
			}
			return static_cast<int64_t>(value);
		}

		bool checked_chunk_tile_origin(const int64_t chunk, int64_t& origin)
		{
			if (chunk > std::numeric_limits<int64_t>::max() / 8 ||
				chunk < std::numeric_limits<int64_t>::min() / 8)
			{
				return false;
			}
			origin = chunk * 8;
			return true;
		}

		using TileKey = std::pair<int64_t, int64_t>;

		struct IndexedLayerTile
		{
			const VegetationLayerTile* tile = nullptr;
			size_t canonical_record_offset = 0;
			size_t canonical_record_bytes = 0;
		};

		using LayerTileIndex = std::map<TileKey, IndexedLayerTile>;

		bool build_layer_tile_index(
			const VegetationLayerSnapshot& layer,
			const std::vector<uint8_t>& canonical_layer,
			LayerTileIndex& out_index)
		{
			if (canonical_layer.size() < 80)
			{
				return false;
			}
			size_t offset = 80;
			for (const VegetationPaletteEntry& entry : layer.palette)
			{
				const size_t record_bytes = 52u + entry.species_asset_path.size();
				if (record_bytes > canonical_layer.size() - offset)
				{
					return false;
				}
				offset += record_bytes;
			}

			out_index.clear();
			for (const VegetationLayerTile& tile : layer.tiles)
			{
				if (canonical_layer.size() - offset < 24)
				{
					return false;
				}
				const int64_t tile_x = read_i64_at(canonical_layer, offset);
				const int64_t tile_z = read_i64_at(canonical_layer, offset + 8);
				const uint32_t payload_bytes =
					read_u32_at(canonical_layer, offset + 20);
				const size_t record_bytes = 24u + static_cast<size_t>(payload_bytes);
				if (record_bytes > canonical_layer.size() - offset ||
					tile_x != tile.tile_x || tile_z != tile.tile_z ||
					!out_index.emplace(
						TileKey{ tile_z, tile_x },
						IndexedLayerTile{ &tile, offset, record_bytes }).second)
				{
					return false;
				}
				offset += record_bytes;
			}
			return offset == canonical_layer.size() &&
				out_index.size() == layer.tiles.size();
		}

		bool collect_chunk_tiles(
			const LayerTileIndex& tile_index,
			const VegetationChunkCoord chunk,
			std::array<const IndexedLayerTile*, 64>& out_tiles)
		{
			int64_t tile_origin_x = 0;
			int64_t tile_origin_z = 0;
			if (!checked_chunk_tile_origin(chunk.x, tile_origin_x) ||
				!checked_chunk_tile_origin(chunk.z, tile_origin_z))
			{
				return false;
			}
			out_tiles.fill(nullptr);
			for (size_t local_z = 0; local_z < 8; ++local_z)
			{
				for (size_t local_x = 0; local_x < 8; ++local_x)
				{
					const auto found = tile_index.find(TileKey{
						tile_origin_z + static_cast<int64_t>(local_z),
						tile_origin_x + static_cast<int64_t>(local_x) });
					if (found != tile_index.end())
					{
						out_tiles[local_z * 8 + local_x] = &found->second;
					}
				}
			}
			return true;
		}

		bool build_chunk_input_identity(
			const VegetationLayerSnapshot& layer,
			const std::vector<uint8_t>& canonical_layer,
			const std::array<const IndexedLayerTile*, 64>& chunk_tiles,
			const VegetationChunkCoord chunk,
			const VegetationSurfaceIdentity& surface,
			const uint32_t cooker_version,
			VegetationChunkInputIdentity& out_identity)
		{
			std::set<VegetationId> used_ids{};
			out_identity = {};
			out_identity.cooker_version = cooker_version;
			out_identity.layer_id = layer.layer_id;
			out_identity.layer_seed = layer.layer_seed;
			out_identity.chunk = chunk;
			out_identity.surface_identity = surface;
			for (size_t slot = 0; slot < chunk_tiles.size(); ++slot)
			{
				const IndexedLayerTile* indexed = chunk_tiles[slot];
				if (indexed == nullptr)
				{
					continue;
				}
				if (indexed->tile == nullptr ||
					indexed->canonical_record_offset > canonical_layer.size() ||
					indexed->canonical_record_bytes >
						canonical_layer.size() - indexed->canonical_record_offset)
				{
					return false;
				}
				out_identity.logical_tiles[slot].present = true;
				out_identity.logical_tiles[slot].canonical_record.assign(
					canonical_layer.begin() + indexed->canonical_record_offset,
					canonical_layer.begin() + indexed->canonical_record_offset +
						indexed->canonical_record_bytes);
				for (const VegetationLayerPlane& plane : indexed->tile->planes)
				{
					if (plane.kind == VegetationLayerPlaneKind::SpeciesWeight)
					{
						used_ids.insert(plane.species_id);
					}
				}
			}
			for (const VegetationId& id : used_ids)
			{
				const auto palette = std::lower_bound(
					layer.palette.begin(), layer.palette.end(), id,
					[](const VegetationPaletteEntry& entry, const VegetationId& value)
					{
						return entry.species_id < value;
					});
				if (palette == layer.palette.end() || palette->species_id != id)
				{
					return false;
				}
				out_identity.used_species.push_back(*palette);
			}
			return true;
		}

		bool round_ties_even_i64(const double value, int64_t& out)
		{
			constexpr double signed_64_min = -9223372036854775808.0;
			constexpr double signed_64_max_exclusive = 9223372036854775808.0;
			if (!std::isfinite(value) ||
				value < signed_64_min || value >= signed_64_max_exclusive)
			{
				return false;
			}
			const double lower_double = std::floor(value);
			int64_t lower = static_cast<int64_t>(lower_double);
			const double fraction = value - lower_double;
			if (fraction > 0.5 || (fraction == 0.5 && (lower & 1) != 0))
			{
				if (lower == std::numeric_limits<int64_t>::max())
				{
					return false;
				}
				++lower;
			}
			out = lower;
			return true;
		}

		bool encode_normal_oct(
			const glm::dvec3& normal,
			int16_t& out_x,
			int16_t& out_y)
		{
			const double denominator =
				std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
			if (!std::isfinite(denominator) || denominator <= 0.0)
			{
				return false;
			}
			double x = normal.x / denominator;
			double y = normal.z / denominator;
			if (normal.y < 0.0)
			{
				const double old_x = x;
				x = (1.0 - std::abs(y)) * (old_x < 0.0 ? -1.0 : 1.0);
				y = (1.0 - std::abs(old_x)) * (y < 0.0 ? -1.0 : 1.0);
			}
			int64_t quantized_x = 0;
			int64_t quantized_y = 0;
			if (!round_ties_even_i64(x * 32767.0, quantized_x) ||
				!round_ties_even_i64(y * 32767.0, quantized_y) ||
				quantized_x < -32767 || quantized_x > 32767 ||
				quantized_y < -32767 || quantized_y > 32767)
			{
				return false;
			}
			out_x = static_cast<int16_t>(quantized_x);
			out_y = static_cast<int16_t>(quantized_y);
			return true;
		}

		uint16_t scale_q12(
			const VegetationPlacement& placement,
			const uint64_t random_stream_four)
		{
			if (placement.min_scale_q12 == placement.max_scale_q12)
			{
				return placement.min_scale_q12;
			}
			const uint32_t random = static_cast<uint32_t>(random_stream_four >> 48);
			const uint32_t numerator =
				static_cast<uint32_t>(placement.max_scale_q12 - placement.min_scale_q12) * random;
			uint32_t rounded = numerator / 65535u;
			if ((numerator % 65535u) * 2u > 65535u)
			{
				++rounded;
			}
			return static_cast<uint16_t>(placement.min_scale_q12 + rounded);
		}

		struct PendingCandidate
		{
			VegetationId species_id{};
			const VegetationSpecies* species = nullptr;
			VegetationChunkInstance instance{};
			VegetationSurfaceSampleRequest request{};
		};

		struct AcceptedInstance
		{
			VegetationId species_id{};
			VegetationChunkInstance instance{};
		};
	}

	VegetationCounterHashResult make_vegetation_counter_hash(
		const VegetationCounterHashKey& key,
		const uint32_t cooker_version)
	{
		const std::array<uint64_t, 10> words{
			read_u64_le(key.layer_id, 0),
			read_u64_le(key.layer_id, 8),
			static_cast<uint64_t>(key.chunk.x),
			static_cast<uint64_t>(key.chunk.z),
			key.cell_x,
			key.cell_z,
			read_u64_le(key.species_id, 0),
			read_u64_le(key.species_id, 8),
			key.layer_seed,
			key.candidate_ordinal
		};
		VegetationCounterHashResult result{};
		result.state = 0x6a09e667f3bcc909ull ^ cooker_version;
		for (const uint64_t word : words)
		{
			result.state = splitmix64(result.state ^ splitmix64(word));
		}
		for (size_t stream = 0; stream < result.random.size(); ++stream)
		{
			result.random[stream] = splitmix64(result.state ^
				(0xd1b54a32d192ed03ull * static_cast<uint64_t>(stream + 1)));
		}
		return result;
	}

	uint32_t vegetation_candidate_accept_limit(const uint8_t effective_threshold)
	{
		const uint32_t numerator = static_cast<uint32_t>(effective_threshold) * 65536u;
		uint32_t rounded = numerator / 255u;
		if ((numerator % 255u) * 2u > 255u)
		{
			++rounded;
		}
		return rounded;
	}

	VegetationSha256 build_vegetation_chunk_input_digest(
		const VegetationChunkInputIdentity& input,
		std::vector<uint8_t>* out_preimage)
	{
		VegetationSha256 empty{};
		if (out_preimage != nullptr)
		{
			out_preimage->clear();
		}
		if (input.cooker_version != 1 || all_zero(input.layer_id) ||
			all_zero(input.surface_identity.surface_id) ||
			!validate_palette(input.used_species, true, nullptr))
		{
			return empty;
		}

		std::vector<uint8_t> bytes{ 'A', 'S', 'V', 'I' };
		append_u16(bytes, 1);
		append_u16(bytes, 0);
		append_u32(bytes, input.cooker_version);
		append_u32(bytes, 32);
		append_u32(bytes, 3200);
		append_u32(bytes, 0);
		bytes.insert(bytes.end(), input.layer_id.begin(), input.layer_id.end());
		append_u64(bytes, input.layer_seed);
		append_u64(bytes, static_cast<uint64_t>(input.chunk.x));
		append_u64(bytes, static_cast<uint64_t>(input.chunk.z));
		bytes.insert(bytes.end(), input.surface_identity.surface_id.begin(),
			input.surface_identity.surface_id.end());
		append_u64(bytes, input.surface_identity.content_revision);
		append_u64(bytes, input.surface_identity.residency_revision);
		append_u64(bytes, input.surface_identity.transform_revision);
		append_u32(bytes, 64);
		for (size_t slot = 0; slot < input.logical_tiles.size(); ++slot)
		{
			const VegetationChunkInputTileRecord& tile = input.logical_tiles[slot];
			if ((tile.present && (tile.canonical_record.empty() ||
				tile.canonical_record.size() > std::numeric_limits<uint32_t>::max())) ||
				(!tile.present && !tile.canonical_record.empty()))
			{
				return empty;
			}
			bytes.push_back(static_cast<uint8_t>(slot));
			bytes.push_back(tile.present ? 1u : 0u);
			append_u16(bytes, 0);
			append_u32(bytes, static_cast<uint32_t>(tile.canonical_record.size()));
			bytes.insert(bytes.end(), tile.canonical_record.begin(), tile.canonical_record.end());
		}
		append_u32(bytes, static_cast<uint32_t>(input.used_species.size()));
		ByteWriter palette_writer{};
		if (!write_palette(input.used_species, palette_writer, nullptr))
		{
			return empty;
		}
		bytes.insert(bytes.end(), palette_writer.bytes.begin(), palette_writer.bytes.end());
		const VegetationSha256 digest = vegetation_sha256(bytes.data(), bytes.size());
		if (out_preimage != nullptr)
		{
			*out_preimage = std::move(bytes);
		}
		return digest;
	}

	static VegetationBakeResult bake_vegetation_chunks_impl(
		const VegetationBakeInput& input,
		VegetationOperationControl control)
	{
		if (!operation_active(control) || input.cooker_version != 1 ||
			input.operation_serial == 0 || !input.layer_snapshot ||
			!input.surface_snapshot || !validate_source_active_identity(input))
		{
			return failed_bake(control, "Vegetation bake input or operation control is invalid");
		}

		std::vector<uint8_t> canonical_layer{};
		std::string codec_error{};
		if (!encode_vegetation_layer(*input.layer_snapshot, canonical_layer, &codec_error))
		{
			return failed_bake(control, "Vegetation bake Layer is invalid: " + codec_error);
		}
		LayerTileIndex tile_index{};
		if (!build_layer_tile_index(
			*input.layer_snapshot, canonical_layer, tile_index))
		{
			return failed_bake(control,
				"Vegetation bake Layer tile index could not be built");
		}

		std::map<VegetationId, const VegetationSpecies*> species_by_id{};
		for (const std::shared_ptr<const VegetationSpecies>& snapshot : input.species_snapshots)
		{
			if (!snapshot || snapshot->placement.candidates_per_cell == 0 ||
				snapshot->placement.candidates_per_cell > 256)
			{
				return failed_bake(control, "Vegetation bake Species is invalid");
			}
			std::vector<uint8_t> canonical_species{};
			if (!encode_vegetation_species(*snapshot, canonical_species, &codec_error) ||
				!species_by_id.emplace(snapshot->species_id, snapshot.get()).second)
			{
				return failed_bake(control, "Vegetation bake Species is invalid: " + codec_error);
			}
			const auto palette = std::lower_bound(
				input.layer_snapshot->palette.begin(), input.layer_snapshot->palette.end(),
				snapshot->species_id,
				[](const VegetationPaletteEntry& entry, const VegetationId& id)
				{
					return entry.species_id < id;
				});
			if (palette == input.layer_snapshot->palette.end() ||
				palette->species_id != snapshot->species_id ||
				palette->species_sha256 != vegetation_sha256(
					canonical_species.data(), canonical_species.size()))
			{
				return failed_bake(control, "Vegetation bake Species does not match Layer palette");
			}
		}
		if (species_by_id.size() != input.layer_snapshot->palette.size())
		{
			return failed_bake(control,
				"Vegetation bake Species snapshots do not cover the complete Layer palette");
		}

		VegetationSurfaceIdentity surface_identity{};
		try
		{
			surface_identity = input.surface_snapshot->identity();
		}
		catch (...)
		{
			return failed_bake(control, "Vegetation bake surface identity failed");
		}
		if (all_zero(surface_identity.surface_id))
		{
			return failed_bake(control, "Vegetation bake surface identity is invalid");
		}
		auto surface_still_matches_initial = [&]()
		{
			try
			{
				return surface_identities_equal(
					input.surface_snapshot->identity(), surface_identity);
			}
			catch (...)
			{
				return false;
			}
		};

		VegetationBakeResult result{};
		VegetationBakeTransactionOutput transaction{};
		if (input.active_chunk_set && !validate_active_snapshot(*input.active_chunk_set))
		{
			return failed_bake(control, "Vegetation bake active chunk-set snapshot is invalid");
		}
		const bool have_active = input.active_chunk_set != nullptr;
		const bool active_layer_matches = have_active &&
			input.active_chunk_set->layer_id == input.layer_snapshot->layer_id;
		const bool active_surface_matches = have_active && surface_identities_equal(
			input.active_chunk_set->surface_identity, surface_identity);
		const bool active_generation_matches = have_active &&
			input.active_chunk_set->layer_generation ==
				input.layer_snapshot->content_generation;
		const bool has_localized_evidence =
			!input.dirty_evidence.density_coords.empty() ||
			!input.dirty_evidence.species_coords.empty();
		const bool localized_evidence_complete = has_localized_evidence &&
			input.dirty_evidence.generation == input.layer_snapshot->content_generation &&
			have_active && input.dirty_evidence.base_generation ==
				input.active_chunk_set->layer_generation;
		transaction.full_rebake_required = !have_active || !active_layer_matches ||
			!active_surface_matches ||
			(!active_generation_matches && !localized_evidence_complete);

		std::vector<VegetationChunkCoord> dirty{};
		if (transaction.full_rebake_required)
		{
			if (have_active)
			{
				for (const VegetationActiveChunkSetEntrySummary& entry :
					input.active_chunk_set->entries)
				{
					dirty.push_back(entry.coord);
				}
			}
			for (const VegetationLayerTile& tile : input.layer_snapshot->tiles)
			{
				const bool has_nonzero_density = std::any_of(
					tile.planes.front().values.begin(), tile.planes.front().values.end(),
					[](const uint8_t value)
					{
						return value != 0;
					});
				if (has_nonzero_density)
				{
					dirty.push_back({
						floor_divide_by_eight(tile.tile_x),
						floor_divide_by_eight(tile.tile_z) });
				}
			}
		}
		else
		{
			dirty = input.dirty_evidence.density_coords;
			for (const VegetationAuthoringSpeciesDirtyEvidence& species_evidence :
				input.dirty_evidence.species_coords)
			{
				dirty.insert(dirty.end(), species_evidence.before_coords.begin(),
					species_evidence.before_coords.end());
				dirty.insert(dirty.end(), species_evidence.after_coords.begin(),
					species_evidence.after_coords.end());
				if (have_active)
				{
					for (const VegetationActiveChunkSetEntrySummary& entry :
						input.active_chunk_set->entries)
					{
						if (std::binary_search(entry.referenced_species_ids.begin(),
							entry.referenced_species_ids.end(), species_evidence.species_id))
						{
							dirty.push_back(entry.coord);
						}
					}
				}
			}
		}
		std::sort(dirty.begin(), dirty.end(), chunk_coord_less);
		dirty.erase(std::unique(dirty.begin(), dirty.end(), chunk_coord_equal), dirty.end());
		for (const VegetationChunkCoord chunk_coord : dirty)
		{
			if (!operation_active(control))
			{
				return failed_bake(control, "Vegetation bake was cancelled or expired");
			}
			std::array<const IndexedLayerTile*, 64> chunk_tiles{};
			if (!collect_chunk_tiles(tile_index, chunk_coord, chunk_tiles))
			{
				return failed_bake(control, "Vegetation bake chunk coordinate overflowed");
			}

			std::vector<PendingCandidate> pending{};
			pending.reserve(4096);
			std::vector<AcceptedInstance> accepted{};
			uint64_t candidate_attempts = 0;
			std::string batch_error{};
			auto flush_pending = [&]()
			{
				if (pending.empty())
				{
					return true;
				}
				if (!surface_still_matches_initial())
				{
					batch_error = "Vegetation bake surface identity changed before sampling";
					return false;
				}
				std::vector<VegetationSurfaceSampleRequest> requests{};
				requests.reserve(pending.size());
				for (const PendingCandidate& candidate : pending)
				{
					requests.push_back(candidate.request);
				}
				const VegetationSurfaceBatchResult batch = sample_vegetation_surface_batch(
					*input.surface_snapshot, requests, control);
				if (batch.status == VegetationSurfaceStatus::Pending ||
					batch.status == VegetationSurfaceStatus::Failed ||
					batch.samples.size() != requests.size())
				{
					batch_error = "Vegetation bake surface batch failed";
					if (!batch.detail.empty())
					{
						batch_error += ": " + batch.detail;
					}
					return false;
				}
				if (!surface_still_matches_initial())
				{
					batch_error = "Vegetation bake surface identity changed after sampling";
					return false;
				}
				for (size_t local = 0; local < batch.samples.size(); ++local)
				{
					const VegetationSurfaceSample& sample = batch.samples[local];
					if (sample.status == VegetationSurfaceStatus::Outside)
					{
						continue;
					}
					if (sample.status != VegetationSurfaceStatus::Ready)
					{
						batch_error = "Vegetation bake surface sample is not ready";
						return false;
					}
					PendingCandidate& candidate = pending[local];
					glm::dvec3 normalized{};
					uint16_t slope = 0;
					if (!evaluate_vegetation_surface_normal(
						sample.world_normal, normalized, slope) ||
						slope < candidate.species->placement.min_slope_milliradians ||
						slope > candidate.species->placement.max_slope_milliradians)
					{
						continue;
					}
					bool material_matches = true;
					for (size_t slot = 0; slot < sample.material_slot_weights.size(); ++slot)
					{
						const uint8_t value = sample.material_slot_weights[slot];
						material_matches = material_matches &&
							value >= candidate.species->placement.material_slot_min[slot] &&
							value <= candidate.species->placement.material_slot_max[slot];
					}
					if (!material_matches)
					{
						continue;
					}
					int64_t height = 0;
					if (!round_ties_even_i64(sample.world_height_meters * 1000.0, height) ||
						height < std::numeric_limits<int32_t>::min() ||
						height > std::numeric_limits<int32_t>::max())
					{
						batch_error = "Vegetation bake surface height is outside the ASVC domain";
						return false;
					}
					if (!encode_normal_oct(normalized,
						candidate.instance.normal_oct_x,
						candidate.instance.normal_oct_y))
					{
						batch_error = "Vegetation bake surface normal could not be quantized";
						return false;
					}
					if (accepted.size() >= std::numeric_limits<uint32_t>::max())
					{
						batch_error = "Vegetation bake ASVC instance count exceeds uint32";
						return false;
					}
					candidate.instance.world_height_mm = static_cast<int32_t>(height);
					accepted.push_back({ candidate.species_id, candidate.instance });
				}
				pending.clear();
				return true;
			};
			for (uint16_t cell_z = 0; cell_z < 256; ++cell_z)
			{
				for (uint16_t cell_x = 0; cell_x < 256; ++cell_x)
				{
					if (!operation_active(control))
					{
						return failed_bake(control,
							"Vegetation bake was cancelled or expired during candidate generation");
					}
					const IndexedLayerTile* indexed_tile = chunk_tiles[
						static_cast<size_t>(cell_z / 32) * 8 + cell_x / 32];
					if (indexed_tile == nullptr || indexed_tile->tile == nullptr)
					{
						continue;
					}
					const VegetationLayerTile& tile = *indexed_tile->tile;
					const size_t texel = static_cast<size_t>(cell_z % 32) * 32 + cell_x % 32;
					const uint8_t density = tile.planes.front().values[texel];
					if (density == 0)
					{
						continue;
					}
					for (size_t plane_index = 1; plane_index < tile.planes.size(); ++plane_index)
					{
						const VegetationLayerPlane& plane = tile.planes[plane_index];
						const uint8_t weight = plane.values[texel];
						const uint8_t threshold = static_cast<uint8_t>(
							(static_cast<uint32_t>(density) * weight + 127u) / 255u);
						if (threshold == 0)
						{
							continue;
						}
						const auto species = species_by_id.find(plane.species_id);
						if (species == species_by_id.end())
						{
							return failed_bake(control, "Vegetation bake is missing a referenced Species");
						}
						for (uint16_t ordinal = 0;
							ordinal < species->second->placement.candidates_per_cell; ++ordinal)
						{
							if (candidate_attempts == std::numeric_limits<uint64_t>::max())
							{
								return failed_bake(control,
									"Vegetation bake candidate attempt count overflowed");
							}
							++candidate_attempts;
							if (candidate_attempts % 4096u == 0)
							{
								if (!operation_active(control))
								{
									return failed_bake(control,
										"Vegetation bake was cancelled or expired during candidate generation");
								}
								if (!surface_still_matches_initial())
								{
									return failed_bake(control,
										"Vegetation bake surface identity changed during candidate generation");
								}
							}
							VegetationCounterHashKey key{};
							key.layer_id = input.layer_snapshot->layer_id;
							key.chunk = chunk_coord;
							key.cell_x = cell_x;
							key.cell_z = cell_z;
							key.species_id = plane.species_id;
							key.layer_seed = input.layer_snapshot->layer_seed;
							key.candidate_ordinal = ordinal;
							const VegetationCounterHashResult hash =
								make_vegetation_counter_hash(key, input.cooker_version);
							if (static_cast<uint32_t>(hash.random[0] >> 48) >=
								vegetation_candidate_accept_limit(threshold))
							{
								continue;
							}
							PendingCandidate candidate{};
							candidate.species_id = plane.species_id;
							candidate.species = species->second;
							candidate.instance.cell_x = cell_x;
							candidate.instance.cell_z = cell_z;
							candidate.instance.candidate_ordinal = ordinal;
							candidate.instance.cell_fraction_x_u16 =
								static_cast<uint16_t>(hash.random[1] >> 48);
							candidate.instance.cell_fraction_z_u16 =
								static_cast<uint16_t>(hash.random[2] >> 48);
							candidate.instance.yaw_turn_u16 =
								static_cast<uint16_t>(hash.random[3] >> 48);
							candidate.instance.scale_q12 = scale_q12(
								species->second->placement, hash.random[4]);
							candidate.request.chunk = chunk_coord;
							candidate.request.local_xz = {
								static_cast<double>(cell_x) +
									candidate.instance.cell_fraction_x_u16 / 65536.0,
								static_cast<double>(cell_z) +
									candidate.instance.cell_fraction_z_u16 / 65536.0 };
							pending.push_back(std::move(candidate));
							if (pending.size() == 4096 && !flush_pending())
							{
								return failed_bake(control, std::move(batch_error));
							}
						}
					}
				}
			}

			if (!flush_pending())
			{
				return failed_bake(control, std::move(batch_error));
			}
			if (accepted.empty())
			{
				transaction.removed_coords.push_back(chunk_coord);
				continue;
			}

			std::sort(accepted.begin(), accepted.end(), [](const AcceptedInstance& lhs,
				const AcceptedInstance& rhs)
			{
				return std::tie(lhs.species_id, lhs.instance.cell_z, lhs.instance.cell_x,
					lhs.instance.candidate_ordinal) <
					std::tie(rhs.species_id, rhs.instance.cell_z, rhs.instance.cell_x,
						rhs.instance.candidate_ordinal);
			});
			std::vector<VegetationId> used_ids{};
			for (const AcceptedInstance& value : accepted)
			{
				if (used_ids.empty() || used_ids.back() != value.species_id)
				{
					used_ids.push_back(value.species_id);
				}
			}

			VegetationChunkInputIdentity identity{};
			if (!build_chunk_input_identity(*input.layer_snapshot, canonical_layer,
				chunk_tiles, chunk_coord, surface_identity, input.cooker_version,
				identity))
			{
				return failed_bake(control, "Vegetation bake ASVI input could not be built");
			}
			const VegetationSha256 input_digest =
				build_vegetation_chunk_input_digest(identity, nullptr);
			if (std::all_of(input_digest.begin(), input_digest.end(), [](const uint8_t byte)
				{ return byte == 0; }))
			{
				return failed_bake(control, "Vegetation bake ASVI input is invalid");
			}

			VegetationChunk chunk{};
			chunk.cooker_version = input.cooker_version;
			chunk.layer_id = input.layer_snapshot->layer_id;
			chunk.chunk_input_sha256 = input_digest;
			chunk.chunk = chunk_coord;
			chunk.surface_identity = surface_identity;
			for (const VegetationId& id : used_ids)
			{
				const auto palette = std::lower_bound(
					input.layer_snapshot->palette.begin(), input.layer_snapshot->palette.end(), id,
					[](const VegetationPaletteEntry& entry, const VegetationId& value)
					{
						return entry.species_id < value;
					});
				if (palette == input.layer_snapshot->palette.end() || palette->species_id != id)
				{
					return failed_bake(control, "Vegetation bake palette remap failed");
				}
				chunk.species.push_back(*palette);
			}
			for (AcceptedInstance& value : accepted)
			{
				const auto species = std::lower_bound(used_ids.begin(), used_ids.end(), value.species_id);
				value.instance.species_index = static_cast<uint16_t>(species - used_ids.begin());
				chunk.instances.push_back(value.instance);
			}
			chunk.min_world_height_mm = std::numeric_limits<int32_t>::max();
			chunk.max_world_height_mm = std::numeric_limits<int32_t>::min();
			for (const VegetationChunkInstance& instance : chunk.instances)
			{
				chunk.min_world_height_mm = std::min(
					chunk.min_world_height_mm, instance.world_height_mm);
				chunk.max_world_height_mm = std::max(
					chunk.max_world_height_mm, instance.world_height_mm);
			}

			VegetationBakedChunk baked{};
			baked.coord = chunk_coord;
			baked.input_digest = input_digest;
			baked.chunk = std::move(chunk);
			if (!encode_vegetation_chunk(baked.chunk, baked.object_bytes, &codec_error))
			{
				return failed_bake(control, "Vegetation bake Chunk encode failed: " + codec_error);
			}
			baked.object_sha256 = vegetation_sha256(
				baked.object_bytes.data(), baked.object_bytes.size());
			transaction.chunks.push_back(std::move(baked));
		}

		std::map<VegetationChunkCoord, VegetationChunkSetManifestEntry,
			bool (*)(const VegetationChunkCoord&, const VegetationChunkCoord&)>
			resulting_entries(&chunk_coord_less);
		if (have_active)
		{
			for (const VegetationActiveChunkSetEntrySummary& active_entry :
				input.active_chunk_set->entries)
			{
				VegetationChunkSetManifestEntry manifest_entry{};
				manifest_entry.coord = active_entry.coord;
				manifest_entry.object_sha256 = active_entry.object_sha256;
				manifest_entry.input_sha256 = active_entry.input_sha256;
				resulting_entries.emplace(manifest_entry.coord, std::move(manifest_entry));
			}
		}
		for (const VegetationBakedChunk& baked : transaction.chunks)
		{
			VegetationChunkSetManifestEntry manifest_entry{};
			manifest_entry.coord = baked.coord;
			manifest_entry.object_sha256 = baked.object_sha256;
			manifest_entry.input_sha256 = baked.input_digest;
			resulting_entries.insert_or_assign(
				manifest_entry.coord, std::move(manifest_entry));
		}
		for (const VegetationChunkCoord removed : transaction.removed_coords)
		{
			resulting_entries.erase(removed);
		}

		transaction.resulting_manifest.layer_id = input.layer_snapshot->layer_id;
		transaction.resulting_manifest.layer_generation =
			input.layer_snapshot->content_generation;
		transaction.resulting_manifest.surface_identity = surface_identity;
		transaction.resulting_manifest.entries.reserve(resulting_entries.size());
		for (const auto& [coord, entry] : resulting_entries)
		{
			(void)coord;
			transaction.resulting_manifest.entries.push_back(entry);
		}
		transaction.source_active_identity = input.source_active_identity;
		transaction.expected_identity.operation_serial = input.operation_serial;
		transaction.expected_identity.cooker_version = input.cooker_version;
		transaction.expected_identity.format_version = 1;
		transaction.expected_identity.layer_id = input.layer_snapshot->layer_id;
		transaction.expected_identity.layer_generation =
			input.layer_snapshot->content_generation;
		transaction.expected_identity.surface_identity = surface_identity;
		transaction.expected_identity.species_identities.reserve(
			input.layer_snapshot->palette.size());
		for (const VegetationPaletteEntry& palette : input.layer_snapshot->palette)
		{
			transaction.expected_identity.species_identities.push_back({
				palette.species_id, palette.species_sha256 });
		}
		transaction.expected_identity.target_coords = dirty;

		if (!operation_active(control) || !surface_still_matches_initial())
		{
			return failed_bake(control,
				"Vegetation bake was cancelled, expired, or observed a changed surface identity");
		}
		result.status = VegetationBakeStatus::Succeeded;
		result.transaction.emplace(std::move(transaction));
		return result;
	}

	VegetationBakeResult bake_vegetation_chunks(
		const VegetationBakeInput& input,
		VegetationOperationControl control)
	{
		try
		{
			return bake_vegetation_chunks_impl(input, control);
		}
		catch (const std::bad_alloc&)
		{
			return failed_bake(control, "Vegetation bake allocation failed");
		}
		catch (const std::length_error&)
		{
			return failed_bake(control, "Vegetation bake size exceeded a container limit");
		}
	}
}
