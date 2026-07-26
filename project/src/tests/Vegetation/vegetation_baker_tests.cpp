#include "Function/Asset/VegetationChunkSet.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/VegetationBaker.h"
#include "Function/Asset/VegetationBrush.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
	AshEngine::VegetationCounterHashKey SecondCounterKey(
		const uint64_t layer_seed = 0x0123456789abcdefull)
	{
		AshEngine::VegetationCounterHashKey key{};
		key.layer_id = VegetationTest::SequentialId(0x00);
		key.chunk = { -2, 3 };
		key.cell_x = 17;
		key.cell_z = 29;
		key.species_id = VegetationTest::SequentialId(0x10);
		key.layer_seed = layer_seed;
		key.candidate_ordinal = 5;
		return key;
	}

	class BatchedSurfaceSnapshot final : public AshEngine::IVegetationSurfaceSnapshot
	{
	public:
		enum class SecondBatchBehavior
		{
			Ready,
			Pending,
			Fail,
			Cancel
		};

		AshEngine::VegetationSurfaceIdentity surface_identity =
			VegetationTest::SurfaceIdentity(0x60, 7, 8, 9);
		SecondBatchBehavior second_batch_behavior = SecondBatchBehavior::Ready;
		std::shared_ptr<std::atomic_bool> cancellation{};
		size_t change_identity_on_call = 0;
		mutable size_t batch_count = 0;
		mutable size_t max_batch_size = 0;
		mutable size_t identity_call_count = 0;
		mutable std::vector<AshEngine::VegetationChunkCoord> batch_chunks{};

		AshEngine::VegetationSurfaceIdentity identity() const override
		{
			++identity_call_count;
			AshEngine::VegetationSurfaceIdentity result = surface_identity;
			if (change_identity_on_call != 0 &&
				identity_call_count >= change_identity_on_call)
			{
				++result.content_revision;
			}
			return result;
		}

		AshEngine::VegetationSurfaceBounds bounds() const override
		{
			return { { -1024, -1024 }, { 1024, 1024 } };
		}

		AshEngine::VegetationSurfaceBatchResult sample_batch(
			const std::vector<AshEngine::VegetationSurfaceSampleRequest>& requests,
			AshEngine::VegetationOperationControl) const override
		{
			++batch_count;
			max_batch_size = std::max(max_batch_size, requests.size());
			if (!requests.empty())
			{
				batch_chunks.push_back(requests.front().chunk);
			}
			if (batch_count == 2 && second_batch_behavior == SecondBatchBehavior::Fail)
			{
				throw std::runtime_error("second batch fixture failure");
			}
			if (batch_count == 2 && second_batch_behavior == SecondBatchBehavior::Cancel)
			{
				cancellation->store(true, std::memory_order_release);
			}

			AshEngine::VegetationSurfaceBatchResult result{};
			result.status = AshEngine::VegetationSurfaceStatus::Ready;
			result.samples.reserve(requests.size());
			for (uint32_t index = 0; index < requests.size(); ++index)
			{
				if (batch_count == 2 &&
					second_batch_behavior == SecondBatchBehavior::Pending)
				{
					result.samples.push_back(VegetationTest::NonReadySurfaceSample(
						index, AshEngine::VegetationSurfaceStatus::Pending));
				}
				else
				{
					result.samples.push_back(VegetationTest::ReadySurfaceSample(
						index, 1.25, { 0.0, 1.0, 0.0 }));
				}
			}
			if (batch_count == 2 &&
				second_batch_behavior == SecondBatchBehavior::Pending)
			{
				result.status = AshEngine::VegetationSurfaceStatus::Pending;
				result.detail = "second batch remains pending";
			}
			return result;
		}
	};

	void AppendU16(std::vector<uint8_t>& bytes, const uint16_t value)
	{
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}

	void AppendU32(std::vector<uint8_t>& bytes, const uint32_t value)
	{
		for (uint32_t shift = 0; shift < 32; shift += 8)
		{
			bytes.push_back(static_cast<uint8_t>(value >> shift));
		}
	}

	void AppendU64(std::vector<uint8_t>& bytes, const uint64_t value)
	{
		for (uint32_t shift = 0; shift < 64; shift += 8)
		{
			bytes.push_back(static_cast<uint8_t>(value >> shift));
		}
	}

	std::vector<uint8_t> AllAbsentAsviGoldenBytes()
	{
		std::vector<uint8_t> bytes{ 'A', 'S', 'V', 'I' };
		AppendU16(bytes, 1);
		AppendU16(bytes, 0);
		AppendU32(bytes, 1);
		AppendU32(bytes, 32);
		AppendU32(bytes, 3200);
		AppendU32(bytes, 0);
		for (uint8_t value = 0x00; value <= 0x0f; ++value)
		{
			bytes.push_back(value);
		}
		AppendU64(bytes, 0x0123456789abcdefull);
		AppendU64(bytes, static_cast<uint64_t>(-2));
		AppendU64(bytes, 3);
		for (uint8_t value = 0x10; value <= 0x1f; ++value)
		{
			bytes.push_back(value);
		}
		AppendU64(bytes, 4);
		AppendU64(bytes, 5);
		AppendU64(bytes, 6);
		AppendU32(bytes, 64);
		for (uint8_t slot = 0; slot < 64; ++slot)
		{
			bytes.push_back(slot);
			bytes.push_back(0);
			AppendU16(bytes, 0);
			AppendU32(bytes, 0);
		}
		AppendU32(bytes, 0);
		return bytes;
	}

	void AppendPaletteEntry(
		std::vector<uint8_t>& bytes,
		const AshEngine::VegetationPaletteEntry& entry)
	{
		bytes.insert(bytes.end(), entry.species_id.begin(), entry.species_id.end());
		bytes.insert(bytes.end(), entry.species_sha256.begin(), entry.species_sha256.end());
		AppendU16(bytes, static_cast<uint16_t>(entry.species_asset_path.size()));
		AppendU16(bytes, 0);
		bytes.insert(bytes.end(), entry.species_asset_path.begin(),
			entry.species_asset_path.end());
	}

	std::vector<uint8_t> PresentAsviGoldenBytes(
		const std::array<std::vector<uint8_t>, 64>& records,
		const std::vector<AshEngine::VegetationPaletteEntry>& used_species)
	{
		std::vector<uint8_t> bytes{ 'A', 'S', 'V', 'I' };
		AppendU16(bytes, 1);
		AppendU16(bytes, 0);
		AppendU32(bytes, 1);
		AppendU32(bytes, 32);
		AppendU32(bytes, 3200);
		AppendU32(bytes, 0);
		const AshEngine::VegetationId layer_id = VegetationTest::SequentialId(0x20);
		bytes.insert(bytes.end(), layer_id.begin(), layer_id.end());
		AppendU64(bytes, 0x1020304050607080ull);
		AppendU64(bytes, static_cast<uint64_t>(-7));
		AppendU64(bytes, 11);
		const AshEngine::VegetationId surface_id = VegetationTest::SequentialId(0x40);
		bytes.insert(bytes.end(), surface_id.begin(), surface_id.end());
		AppendU64(bytes, 12);
		AppendU64(bytes, 13);
		AppendU64(bytes, 14);
		AppendU32(bytes, 64);
		for (uint8_t slot = 0; slot < 64; ++slot)
		{
			bytes.push_back(slot);
			bytes.push_back(records[slot].empty() ? 0u : 1u);
			AppendU16(bytes, 0);
			AppendU32(bytes, static_cast<uint32_t>(records[slot].size()));
			bytes.insert(bytes.end(), records[slot].begin(), records[slot].end());
		}
		AppendU32(bytes, static_cast<uint32_t>(used_species.size()));
		for (const AshEngine::VegetationPaletteEntry& entry : used_species)
		{
			AppendPaletteEntry(bytes, entry);
		}
		return bytes;
	}

	AshEngine::VegetationBakeInput SingleChunkBakeInput(
		const uint64_t seed,
		const uint64_t generation = 7)
	{
		AshEngine::VegetationLayerSnapshot layer = VegetationTest::ResolvedMinimalLayerSnapshot();
		layer.content_generation = generation;
		layer.layer_seed = seed;
		layer.tiles[0].tile_x = 0;
		layer.tiles[0].tile_z = 0;

		AshEngine::VegetationSpecies species{};
		std::string error{};
		if (!AshEngine::decode_vegetation_species(
			VegetationTest::CanonicalGrassSpeciesJson(),
			VegetationTest::GenerousLoadBudget(), species, &error))
		{
			throw std::runtime_error("Single-chunk Species fixture failed to decode: " + error);
		}

		auto surface = std::make_shared<VegetationTest::ScriptedSurfaceSnapshot>();
		surface->identity_before = VegetationTest::SurfaceIdentity(0x10, 4, 5, 6);
		surface->identity_after = surface->identity_before;
		surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;
		for (uint32_t index = 0; index < species.placement.candidates_per_cell; ++index)
		{
			surface->result.samples.push_back(
				VegetationTest::ReadySurfaceSample(index, 1.25, { 0.0, 1.0, 0.0 }));
		}

		AshEngine::VegetationBakeInput input{};
		input.cooker_version = 1;
		input.operation_serial = 9;
		input.layer_snapshot = std::make_shared<const AshEngine::VegetationLayerSnapshot>(
			std::move(layer));
		input.species_snapshots.push_back(
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)));
		input.surface_snapshot = std::move(surface);
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::NoActive;
		input.dirty_evidence.base_generation = input.layer_snapshot->content_generation;
		input.dirty_evidence.generation = input.layer_snapshot->content_generation;
		input.dirty_evidence.density_coords.push_back({ 0, 0 });
		return input;
	}

	AshEngine::VegetationBakeInput SingleCellGoldenBakeInput(
		const uint64_t seed,
		const uint8_t effective_threshold,
		std::vector<AshEngine::VegetationSurfaceSample> samples,
		const uint64_t generation = 7)
	{
		if (samples.empty() || samples.size() > 256)
		{
			throw std::runtime_error(
				"Single-cell golden fixture requires 1..256 surface samples");
		}
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(seed, generation);
		AshEngine::VegetationSpecies species = *input.species_snapshots[0];
		species.placement.candidates_per_cell =
			static_cast<uint16_t>(samples.size());
		species.placement.max_slope_milliradians = 1571;
		std::vector<uint8_t> canonical_species{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(
			species, canonical_species, &error))
		{
			throw std::runtime_error(
				"Single-cell golden Species fixture failed to encode: " + error);
		}

		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
			canonical_species.data(), canonical_species.size());
		for (AshEngine::VegetationLayerPlane& plane : layer.tiles[0].planes)
		{
			plane.values.fill(0);
		}
		layer.tiles[0].planes[0].values[0] = 255;
		layer.tiles[0].planes[1].values[0] = effective_threshold;
		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.species_snapshots = {
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)) };

		auto surface = std::const_pointer_cast<
			VegetationTest::ScriptedSurfaceSnapshot>(
				std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
					input.surface_snapshot));
		if (!surface)
		{
			throw std::runtime_error(
				"Single-cell golden surface fixture could not be recovered");
		}
		surface->result = {};
		surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;
		surface->result.samples = std::move(samples);
		return input;
	}

	std::vector<uint8_t> CanonicalLayerWithNormalizedGenerationAndSeed(
		const AshEngine::VegetationLayerSnapshot& snapshot,
		const uint64_t generation,
		const uint64_t seed)
	{
		AshEngine::VegetationLayerSnapshot normalized = snapshot;
		normalized.content_generation = generation;
		normalized.layer_seed = seed;
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(normalized, bytes, &error))
		{
			throw std::runtime_error(
				"Normalized Layer fixture failed to encode: " + error);
		}
		return bytes;
	}

	AshEngine::VegetationBakeInput BakeInputWithUnusedSecondSpecies(
		const bool mutate_unused_species)
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		AshEngine::VegetationSpecies second = *input.species_snapshots[0];
		second.species_id = VegetationTest::SequentialId(0x80);
		second.name = mutate_unused_species ? "Unused changed" : "Unused";
		std::vector<uint8_t> canonical_species{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(second, canonical_species, &error))
		{
			throw std::runtime_error("Unused Species fixture failed to encode: " + error);
		}

		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		AshEngine::VegetationPaletteEntry palette{};
		palette.species_id = second.species_id;
		palette.species_sha256 = AshEngine::vegetation_sha256(
			canonical_species.data(), canonical_species.size());
		palette.species_asset_path = "vegetation/Unused.AshVegetation";
		layer.palette.push_back(palette);
		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.species_snapshots = {
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(second)),
			input.species_snapshots[0] };
		return input;
	}

	AshEngine::VegetationBakeInput TwoUsedSpeciesBakeInput(
		const bool mutate_second_species = false)
	{
		AshEngine::VegetationBakeInput input =
			BakeInputWithUnusedSecondSpecies(mutate_second_species);
		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		AshEngine::VegetationLayerPlane second_weight{};
		second_weight.kind = AshEngine::VegetationLayerPlaneKind::SpeciesWeight;
		second_weight.species_id = layer.palette[1].species_id;
		second_weight.values.fill(0);
		second_weight.values[0] = 255;
		layer.tiles[0].planes.push_back(std::move(second_weight));
		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));

		auto surface = std::make_shared<VegetationTest::ScriptedSurfaceSnapshot>();
		surface->identity_before = VegetationTest::SurfaceIdentity(0x10, 4, 5, 6);
		surface->identity_after = surface->identity_before;
		surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;
		for (uint32_t index = 0; index < 16; ++index)
		{
			surface->result.samples.push_back(
				VegetationTest::ReadySurfaceSample(
					index, 1.25, { 0.0, 1.0, 0.0 }));
		}
		input.surface_snapshot = std::move(surface);
		return input;
	}

	AshEngine::VegetationBakeInput EmptyPaletteBakeInput(
		const uint64_t generation = 7)
	{
		AshEngine::VegetationLayerSnapshot layer{};
		layer.layer_id = VegetationTest::SequentialId(0x21);
		layer.content_generation = generation;
		layer.layer_seed = 0x123456789abcdef0ull;
		auto surface = std::make_shared<VegetationTest::ScriptedSurfaceSnapshot>();
		surface->identity_before = VegetationTest::SurfaceIdentity(0x40, 7, 8, 9);
		surface->identity_after = surface->identity_before;
		surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;

		AshEngine::VegetationBakeInput input{};
		input.cooker_version = 1;
		input.operation_serial = 11;
		input.layer_snapshot = std::make_shared<const AshEngine::VegetationLayerSnapshot>(
			std::move(layer));
		input.surface_snapshot = std::move(surface);
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::NoActive;
		input.dirty_evidence.base_generation = generation;
		input.dirty_evidence.generation = generation;
		return input;
	}

	AshEngine::VegetationBakeInput DenseTwoBatchBakeInput(
		const std::shared_ptr<BatchedSurfaceSnapshot>& surface)
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x2234u);
		AshEngine::VegetationSpecies species = *input.species_snapshots[0];
		species.placement.candidates_per_cell = 256;
		std::vector<uint8_t> canonical_species{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(species, canonical_species, &error))
		{
			throw std::runtime_error("Dense Species fixture failed to encode: " + error);
		}

		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
			canonical_species.data(), canonical_species.size());
		for (AshEngine::VegetationLayerPlane& plane : layer.tiles[0].planes)
		{
			plane.values.fill(0);
			for (size_t texel = 0; texel < 17; ++texel)
			{
				plane.values[texel] = 255;
			}
		}

		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.species_snapshots = {
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)) };
		input.surface_snapshot = surface;
		return input;
	}

	AshEngine::VegetationBakeInput TwoDirtyChunkBakeInput(
		const std::shared_ptr<BatchedSurfaceSnapshot>& surface)
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x2234u, 7);
		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		AshEngine::VegetationLayerTile second = layer.tiles[0];
		second.tile_x = 8;
		layer.tiles.push_back(std::move(second));
		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.surface_snapshot = surface;
		input.dirty_evidence.density_coords = {
			{ 1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 0 } };
		return input;
	}

	AshEngine::VegetationBakeInput RejectHeavyBakeInput(
		const std::shared_ptr<BatchedSurfaceSnapshot>& surface)
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(1);
		AshEngine::VegetationSpecies species = *input.species_snapshots[0];
		species.placement.candidates_per_cell = 256;
		std::vector<uint8_t> canonical_species{};
		std::string error{};
		if (!AshEngine::encode_vegetation_species(species, canonical_species, &error))
		{
			throw std::runtime_error("Reject-heavy Species fixture failed to encode: " + error);
		}

		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
			canonical_species.data(), canonical_species.size());
		for (AshEngine::VegetationLayerPlane& plane : layer.tiles[0].planes)
		{
			plane.values.fill(0);
		}
		layer.tiles[0].planes[0].values[0] = 255;
		layer.tiles[0].planes[1].values[0] = 1;

		const uint32_t limit = AshEngine::vegetation_candidate_accept_limit(1);
		bool found_seed = false;
		for (uint64_t seed = 1; seed < 1000 && !found_seed; ++seed)
		{
			found_seed = true;
			for (uint16_t ordinal = 0; ordinal < 256; ++ordinal)
			{
				AshEngine::VegetationCounterHashKey key{};
				key.layer_id = layer.layer_id;
				key.species_id = species.species_id;
				key.layer_seed = seed;
				key.candidate_ordinal = ordinal;
				if (static_cast<uint32_t>(
					AshEngine::make_vegetation_counter_hash(key, 1).random[0] >> 48) < limit)
				{
					found_seed = false;
					break;
				}
			}
			if (found_seed)
			{
				layer.layer_seed = seed;
			}
		}
		if (!found_seed)
		{
			throw std::runtime_error("Reject-heavy fixture could not find a deterministic seed");
		}

		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.species_snapshots = {
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)) };
		input.surface_snapshot = surface;
		return input;
	}

	AshEngine::VegetationBakeInput SecondCounterKeyBakeInput(
		const uint64_t layer_seed = 0x0123456789abcdefull)
	{
		AshEngine::VegetationSpecies species{};
		std::string error{};
		if (!AshEngine::decode_vegetation_species(
			VegetationTest::CanonicalGrassSpeciesJson(),
			VegetationTest::GenerousLoadBudget(), species, &error))
		{
			throw std::runtime_error("Second-key Species fixture failed to decode: " + error);
		}
		species.species_id = VegetationTest::SequentialId(0x10);
		species.placement.min_scale_q12 = 3277;
		species.placement.max_scale_q12 = 4915;
		std::vector<uint8_t> canonical_species{};
		if (!AshEngine::encode_vegetation_species(species, canonical_species, &error))
		{
			throw std::runtime_error("Second-key Species fixture failed to encode: " + error);
		}

		AshEngine::VegetationPaletteEntry palette{};
		palette.species_id = species.species_id;
		palette.species_sha256 = AshEngine::vegetation_sha256(
			canonical_species.data(), canonical_species.size());
		palette.species_asset_path = "vegetation/SecondKey.AshVegetation";

		AshEngine::VegetationLayerSnapshot layer{};
		layer.layer_id = VegetationTest::SequentialId(0x00);
		layer.content_generation = 7;
		layer.layer_seed = layer_seed;
		layer.palette.push_back(palette);
		AshEngine::VegetationLayerTile tile{};
		tile.tile_x = -16;
		tile.tile_z = 24;
		AshEngine::VegetationLayerPlane density{};
		density.kind = AshEngine::VegetationLayerPlaneKind::Density;
		density.values.fill(0);
		density.values[29u * 32u + 17u] = 255;
		tile.planes.push_back(density);
		AshEngine::VegetationLayerPlane weight{};
		weight.kind = AshEngine::VegetationLayerPlaneKind::SpeciesWeight;
		weight.species_id = species.species_id;
		weight.values.fill(0);
		weight.values[29u * 32u + 17u] = 255;
		tile.planes.push_back(weight);
		layer.tiles.push_back(std::move(tile));

		auto surface = std::make_shared<VegetationTest::ScriptedSurfaceSnapshot>();
		surface->identity_before = VegetationTest::SurfaceIdentity(0x40, 4, 5, 6);
		surface->identity_after = surface->identity_before;
		surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;
		for (uint32_t index = 0; index < species.placement.candidates_per_cell; ++index)
		{
			surface->result.samples.push_back(
				VegetationTest::ReadySurfaceSample(index, 1.25, { 0.0, 1.0, 0.0 }));
		}

		AshEngine::VegetationBakeInput input{};
		input.cooker_version = 1;
		input.operation_serial = 10;
		input.layer_snapshot = std::make_shared<const AshEngine::VegetationLayerSnapshot>(
			std::move(layer));
		input.species_snapshots.push_back(
			std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)));
		input.surface_snapshot = std::move(surface);
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::NoActive;
		input.dirty_evidence.base_generation = input.layer_snapshot->content_generation;
		input.dirty_evidence.generation = input.layer_snapshot->content_generation;
		input.dirty_evidence.density_coords.push_back({ -2, 3 });
		return input;
	}

	AshEngine::VegetationBakeInput InnerCheckpointBakeInput(
		const std::shared_ptr<BatchedSurfaceSnapshot>& surface)
	{
		AshEngine::VegetationBakeInput input = SecondCounterKeyBakeInput();
		AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
		const AshEngine::VegetationSpecies base_species = *input.species_snapshots[0];
		layer.palette.clear();
		layer.tiles[0].planes.resize(1);
		input.species_snapshots.clear();
		for (uint8_t species_index = 0; species_index < 17; ++species_index)
		{
			AshEngine::VegetationSpecies species = base_species;
			species.species_id = VegetationTest::SequentialId(
				static_cast<uint8_t>(0x10 + species_index));
			species.placement.candidates_per_cell = 256;
			std::vector<uint8_t> canonical_species{};
			std::string error{};
			if (!AshEngine::encode_vegetation_species(species, canonical_species, &error))
			{
				throw std::runtime_error("Checkpoint Species fixture failed to encode: " + error);
			}

			AshEngine::VegetationPaletteEntry palette{};
			palette.species_id = species.species_id;
			palette.species_sha256 = AshEngine::vegetation_sha256(
				canonical_species.data(), canonical_species.size());
			palette.species_asset_path = "vegetation/Checkpoint" +
				std::to_string(species_index) + ".AshVegetation";
			layer.palette.push_back(std::move(palette));

			AshEngine::VegetationLayerPlane weight{};
			weight.kind = AshEngine::VegetationLayerPlaneKind::SpeciesWeight;
			weight.species_id = species.species_id;
			weight.values.fill(0);
			weight.values[29u * 32u + 17u] = 1;
			layer.tiles[0].planes.push_back(std::move(weight));
			input.species_snapshots.push_back(
				std::make_shared<const AshEngine::VegetationSpecies>(std::move(species)));
		}
		input.layer_snapshot =
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
		input.surface_snapshot = surface;
		return input;
	}

	std::shared_ptr<const AshEngine::VegetationActiveChunkSetSnapshot> ActiveSnapshotForCoords(
		AshEngine::VegetationBakeInput& input,
		const uint64_t generation,
		std::vector<AshEngine::VegetationChunkCoord> manifest_coords)
	{
		auto active = std::make_shared<AshEngine::VegetationActiveChunkSetSnapshot>();
		active->layer_id = input.layer_snapshot->layer_id;
		active->layer_generation = generation;
		active->surface_identity = input.surface_snapshot->identity();
		active->manifest_sha256.fill(0x31);
		std::sort(manifest_coords.begin(), manifest_coords.end(),
			[](const AshEngine::VegetationChunkCoord lhs,
				const AshEngine::VegetationChunkCoord rhs)
			{
				return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
			});
		for (size_t index = 0; index < manifest_coords.size(); ++index)
		{
			AshEngine::VegetationActiveChunkSetEntrySummary entry{};
			entry.coord = manifest_coords[index];
			entry.object_sha256.fill(static_cast<uint8_t>(0x41 + index));
			entry.input_sha256.fill(static_cast<uint8_t>(0x51 + index));
			entry.referenced_species_ids.push_back(input.layer_snapshot->palette.empty() ?
				VegetationTest::SequentialId(0x20) :
				input.layer_snapshot->palette[0].species_id);
			active->entries.push_back(std::move(entry));
		}
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::Existing;
		input.source_active_identity.manifest_sha256 = active->manifest_sha256;
		return active;
	}

	std::shared_ptr<const AshEngine::VegetationActiveChunkSetSnapshot> ActiveSnapshotFor(
		AshEngine::VegetationBakeInput& input,
		const uint64_t generation,
		const AshEngine::VegetationChunkCoord manifest_coord)
	{
		return ActiveSnapshotForCoords(input, generation, { manifest_coord });
	}

	const AshEngine::VegetationBakeTransactionOutput& RequireTransaction(
		const AshEngine::VegetationBakeResult& result)
	{
		if (result.status != AshEngine::VegetationBakeStatus::Succeeded ||
			!result.transaction.has_value())
		{
			throw std::runtime_error("Successful bake transaction was required");
		}
		return *result.transaction;
	}

	std::vector<uint8_t> LiteralHexBytes(const std::string_view hex)
	{
		if (hex.size() % 2 != 0)
		{
			throw std::runtime_error("Odd literal hex length");
		}
		auto nibble = [](const char value) -> uint8_t
		{
			if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
			if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
			throw std::runtime_error("Invalid literal hex digit");
		};
		std::vector<uint8_t> bytes{};
		bytes.reserve(hex.size() / 2);
		for (size_t index = 0; index < hex.size(); index += 2)
		{
			bytes.push_back(static_cast<uint8_t>(
				(nibble(hex[index]) << 4) | nibble(hex[index + 1])));
		}
		return bytes;
	}

	AshEngine::VegetationChunkSetManifest ManifestFixture(const bool with_entry)
	{
		AshEngine::VegetationChunkSetManifest manifest{};
		manifest.layer_id = VegetationTest::SequentialId(0x00);
		manifest.layer_generation = 0x0123456789abcdefull;
		manifest.surface_identity = VegetationTest::SurfaceIdentity(0x10, 4, 5, 6);
		if (with_entry)
		{
			AshEngine::VegetationChunkSetManifestEntry entry{};
			entry.coord = { -2, 3 };
			for (size_t index = 0; index < entry.object_sha256.size(); ++index)
			{
				entry.object_sha256[index] = static_cast<uint8_t>(0x20 + index);
				entry.input_sha256[index] = static_cast<uint8_t>(0x40 + index);
			}
			manifest.entries.push_back(entry);
		}
		return manifest;
	}

	const std::vector<uint8_t>& EmptyAsvmGolden()
	{
		static const std::vector<uint8_t> bytes = LiteralHexBytes(
			"4153564d01006000000102030405060708090a0b0c0d0e0fefcdab8967452301"
			"101112131415161718191a1b1c1d1e1f04000000000000000500000000000000"
			"060000000000000000000000000000000000000000000000000000000cccd0d0");
		return bytes;
	}

	const std::vector<uint8_t>& SingleAsvmGolden()
	{
		static const std::vector<uint8_t> bytes = LiteralHexBytes(
			"4153564d01006000000102030405060708090a0b0c0d0e0fefcdab8967452301"
			"101112131415161718191a1b1c1d1e1f04000000000000000500000000000000"
			"060000000000000001000000000000005000000000000000709108b73edbf732"
			"feffffffffffffff0300000000000000202122232425262728292a2b2c2d2e2f"
			"303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f"
			"505152535455565758595a5b5c5d5e5f");
		return bytes;
	}

	const std::vector<uint8_t>& AsvaGolden()
	{
		static const std::vector<uint8_t> bytes = LiteralHexBytes(
			"4153564101003000f76a5a8bfbc4cb7fcae0f481dae76ef4f4c3f4847c4045d4"
			"9391f28f47a827ad00000000f36e3f82");
		return bytes;
	}

	void WriteLiteralU16(std::vector<uint8_t>& bytes, const size_t offset, const uint16_t value)
	{
		bytes[offset] = static_cast<uint8_t>(value);
		bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
	}

	void WriteLiteralU32(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value)
	{
		for (size_t index = 0; index < 4; ++index)
		{
			bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
		}
	}

	void WriteLiteralU64(std::vector<uint8_t>& bytes, const size_t offset, const uint64_t value)
	{
		for (size_t index = 0; index < 8; ++index)
		{
			bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
		}
	}

	void RepairAsvmHeaderCrc(std::vector<uint8_t>& bytes)
	{
		WriteLiteralU32(bytes, 92, 0);
		WriteLiteralU32(bytes, 92, AshEngine::vegetation_crc32(bytes.data(), 96));
	}

	void RepairAsvmPayloadAndHeaderCrc(std::vector<uint8_t>& bytes)
	{
		WriteLiteralU32(bytes, 88, AshEngine::vegetation_crc32(
			bytes.data() + 96, bytes.size() - 96));
		RepairAsvmHeaderCrc(bytes);
	}

	void RepairAsvaCrc(std::vector<uint8_t>& bytes)
	{
		WriteLiteralU32(bytes, 44, AshEngine::vegetation_crc32(bytes.data(), 44));
	}

	AshEngine::VegetationChunkSetManifest TwoEntryManifestFixture()
	{
		AshEngine::VegetationChunkSetManifest manifest = ManifestFixture(true);
		manifest.entries[0].coord = { 7, -1 };
		AshEngine::VegetationChunkSetManifestEntry second = manifest.entries[0];
		second.coord = { -7, 2 };
		second.object_sha256.fill(0x61);
		second.input_sha256.fill(0x81);
		manifest.entries.push_back(second);
		return manifest;
	}

	AshEngine::VegetationChunkSetManifest SameZTwoEntryManifestFixture()
	{
		AshEngine::VegetationChunkSetManifest manifest = ManifestFixture(true);
		manifest.entries[0].coord = { -1, 4 };
		AshEngine::VegetationChunkSetManifestEntry second = manifest.entries[0];
		second.coord = { 1, 4 };
		second.object_sha256.fill(0x62);
		second.input_sha256.fill(0x82);
		manifest.entries.push_back(second);
		return manifest;
	}

	void CheckManifestDecodeRejected(
		const std::vector<uint8_t>& bytes,
		const uint32_t max_entries)
	{
		AshEngine::VegetationChunkSetManifest output = ManifestFixture(true);
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_manifest(
			bytes, max_entries, output, &error));
		CHECK(output.layer_id == AshEngine::VegetationId{});
		CHECK(output.layer_generation == 0);
		CHECK(output.surface_identity.surface_id == AshEngine::VegetationId{});
		CHECK(output.surface_identity.content_revision == 0);
		CHECK(output.surface_identity.residency_revision == 0);
		CHECK(output.surface_identity.transform_revision == 0);
		CHECK(output.entries.empty());
		CHECK_FALSE(error.empty());
	}

	void CheckManifestEncodeRejected(const AshEngine::VegetationChunkSetManifest& manifest)
	{
		std::vector<uint8_t> output{ 0x55 };
		std::string error{};
		CHECK_FALSE(AshEngine::encode_vegetation_chunk_set_manifest(
			manifest, output, &error));
		CHECK(output.empty());
		CHECK_FALSE(error.empty());
	}

	void CheckActiveDecodeRejected(const std::vector<uint8_t>& bytes)
	{
		AshEngine::VegetationChunkSetActivePointer output{};
		output.manifest_sha256.fill(0x55);
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_active_pointer(
			bytes, output, &error));
		CHECK(output.manifest_sha256 == AshEngine::VegetationSha256{});
		CHECK_FALSE(error.empty());
	}

	struct EmptyActiveChunkSetFixture
	{
		std::filesystem::path layer_relative_path =
			"vegetation/Meadow.AshVegetationLayer";
		std::filesystem::path store_relative_path =
			"vegetation/Meadow.AshVegetationLayer.AshVegetationChunks";
		std::filesystem::path active_relative_path =
			store_relative_path / "active.asva";
		std::filesystem::path manifest_relative_path{};
		AshEngine::VegetationSha256 manifest_sha256{};
		std::vector<uint8_t> manifest_bytes = EmptyAsvmGolden();
		std::vector<uint8_t> active_bytes{};

		explicit EmptyActiveChunkSetFixture(const bool controlled_long_path = false)
		{
			if (controlled_long_path)
			{
				layer_relative_path = std::filesystem::path("vegetation") /
					std::string(80, 'p') / std::string(80, 'q') /
					"Meadow.AshVegetationLayer";
				store_relative_path = layer_relative_path;
				store_relative_path += ".AshVegetationChunks";
				active_relative_path = store_relative_path / "active.asva";
			}
			RefreshManifestPointer();
		}

		void RefreshManifestPointer()
		{
			manifest_sha256 = AshEngine::vegetation_sha256(
				manifest_bytes.data(), manifest_bytes.size());
			manifest_relative_path = store_relative_path / "manifests" /
				(VegetationTest::ToHex(manifest_sha256) + ".asvm");
			AshEngine::VegetationChunkSetActivePointer pointer{};
			pointer.manifest_sha256 = manifest_sha256;
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk_set_active_pointer(
				pointer, active_bytes, &error))
			{
				throw std::runtime_error("Empty active chunk-set fixture failed: " + error);
			}
		}

		void Write(
			VegetationTest::ScopedAssetRoot& root,
			const bool write_manifest = true) const
		{
			WriteRelative(root, active_relative_path, active_bytes);
			if (write_manifest)
			{
				WriteRelative(root, manifest_relative_path, manifest_bytes);
			}
		}

		void WriteManifest(
			VegetationTest::ScopedAssetRoot& root,
			const std::vector<uint8_t>& bytes) const
		{
			WriteRelative(root, manifest_relative_path, bytes);
		}

		void WriteAsset(
			VegetationTest::ScopedAssetRoot& root,
			const std::filesystem::path& relative_path,
			const std::vector<uint8_t>& bytes) const
		{
			WriteRelative(root, relative_path, bytes);
		}

		void CreateAssetDirectory(
			VegetationTest::ScopedAssetRoot& root,
			const std::filesystem::path& relative_path) const
		{
			const std::filesystem::path destination = root.Path() / relative_path;
			std::error_code error{};
#if defined(_WIN32)
			std::filesystem::path preferred = destination;
			preferred.make_preferred();
			std::filesystem::create_directories(
				std::filesystem::path(L"\\\\?\\" + preferred.native()), error);
#else
			std::filesystem::create_directories(destination, error);
#endif
			if (error)
			{
				throw std::runtime_error("Failed to create chunk-set fixture directory asset");
			}
		}

	private:
		static void WriteRelative(
			VegetationTest::ScopedAssetRoot& root,
			const std::filesystem::path& relative_path,
			const std::vector<uint8_t>& bytes)
		{
			const std::filesystem::path destination = root.Path() / relative_path;
			std::error_code error{};
#if defined(_WIN32)
			std::filesystem::path preferred_parent = destination.parent_path();
			preferred_parent.make_preferred();
			const std::filesystem::path extended_parent =
				std::filesystem::path(L"\\\\?\\" + preferred_parent.native());
			std::filesystem::create_directories(extended_parent, error);
#else
			std::filesystem::create_directories(destination.parent_path(), error);
#endif
			if (error)
			{
				throw std::runtime_error("Failed to create long-path chunk-set fixture directory");
			}
#if defined(_WIN32)
			std::filesystem::path preferred = destination;
			preferred.make_preferred();
			const std::wstring extended = L"\\\\?\\" + preferred.native();
			const HANDLE file = CreateFileW(
				extended.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				throw std::runtime_error("Failed to open long-path chunk-set fixture");
			}
			DWORD written = 0;
			const bool wrote = WriteFile(file, bytes.data(),
				static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
			const bool closed = CloseHandle(file) != FALSE;
			if (!wrote || written != bytes.size() || !closed)
			{
				throw std::runtime_error("Failed to write long-path chunk-set fixture");
			}
#else
			root.Write(relative_path, bytes);
#endif
		}
	};

	struct SingleObjectActiveChunkSetFixture
	{
		EmptyActiveChunkSetFixture chunk_set{};
		AshEngine::VegetationChunk object_chunk = VegetationTest::ResolvedMinimalChunk();
		std::vector<uint8_t> object_bytes{};
		std::filesystem::path object_relative_path{};
		std::vector<uint8_t> species_bytes = VegetationTest::CanonicalGrassSpeciesJson();

		SingleObjectActiveChunkSetFixture()
		{
			AshEngine::VegetationChunkSetManifest manifest{};
			manifest.layer_id = object_chunk.layer_id;
			manifest.layer_generation = 7;
			manifest.surface_identity = object_chunk.surface_identity;
			AshEngine::VegetationChunkSetManifestEntry entry{};
			entry.coord = object_chunk.chunk;
			entry.input_sha256 = object_chunk.chunk_input_sha256;
			manifest.entries.push_back(entry);
			SetObjectChunk(object_chunk, manifest);
		}

		void SetObjectChunk(const AshEngine::VegetationChunk& chunk)
		{
			AshEngine::VegetationChunkSetManifest manifest{};
			std::string error{};
			if (!AshEngine::decode_vegetation_chunk_set_manifest(
				chunk_set.manifest_bytes, 1, manifest, &error))
			{
				throw std::runtime_error("Single-object manifest fixture failed to decode: " + error);
			}
			SetObjectChunk(chunk, manifest);
		}

		void SetRawObjectBytes(const std::vector<uint8_t>& bytes)
		{
			AshEngine::VegetationChunkSetManifest manifest{};
			std::string error{};
			if (!AshEngine::decode_vegetation_chunk_set_manifest(
				chunk_set.manifest_bytes, 1, manifest, &error))
			{
				throw std::runtime_error("Single-object manifest fixture failed to decode: " + error);
			}
			object_bytes = bytes;
			manifest.entries[0].object_sha256 = AshEngine::vegetation_sha256(
				object_bytes.data(), object_bytes.size());
			RebuildManifest(manifest);
		}

		void Write(
			VegetationTest::ScopedAssetRoot& root,
			const bool write_object = true,
			const bool write_species = true) const
		{
			chunk_set.Write(root);
			if (write_object)
			{
				chunk_set.WriteAsset(root, object_relative_path, object_bytes);
			}
			if (write_species)
			{
				chunk_set.WriteAsset(root,
					VegetationTest::ResolvedMinimalPaletteEntry().species_asset_path,
					species_bytes);
			}
		}

	private:
		void SetObjectChunk(
			const AshEngine::VegetationChunk& chunk,
			AshEngine::VegetationChunkSetManifest& manifest)
		{
			object_chunk = chunk;
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk(object_chunk, object_bytes, &error))
			{
				throw std::runtime_error("Single-object chunk fixture failed to encode: " + error);
			}
			manifest.entries[0].object_sha256 = AshEngine::vegetation_sha256(
				object_bytes.data(), object_bytes.size());
			RebuildManifest(manifest);
		}

		void RebuildManifest(const AshEngine::VegetationChunkSetManifest& manifest)
		{
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk_set_manifest(
				manifest, chunk_set.manifest_bytes, &error))
			{
				throw std::runtime_error("Single-object manifest fixture failed to encode: " + error);
			}
			chunk_set.RefreshManifestPointer();
			object_relative_path = chunk_set.store_relative_path / "objects" /
				(VegetationTest::ToHex(manifest.entries[0].object_sha256) +
					".AshVegetationChunk");
		}
	};

	struct ResolvedSpeciesAssetFixture
	{
		AshEngine::VegetationSpecies species{};
		AshEngine::VegetationPaletteEntry palette{};
		std::vector<uint8_t> bytes{};
	};

	ResolvedSpeciesAssetFixture MakeResolvedSpeciesAsset(
		const AshEngine::VegetationId& species_id,
		const std::string& asset_path,
		const uint16_t candidates_per_cell)
	{
		ResolvedSpeciesAssetFixture fixture{};
		std::string error{};
		if (!AshEngine::decode_vegetation_species(
			VegetationTest::CanonicalGrassSpeciesJson(),
			VegetationTest::GenerousLoadBudget(), fixture.species, &error))
		{
			throw std::runtime_error("Multi-object Species fixture failed to decode: " + error);
		}
		fixture.species.species_id = species_id;
		fixture.species.placement.candidates_per_cell = candidates_per_cell;
		if (!AshEngine::encode_vegetation_species(
			fixture.species, fixture.bytes, &error))
		{
			throw std::runtime_error("Multi-object Species fixture failed to encode: " + error);
		}
		fixture.palette.species_id = fixture.species.species_id;
		fixture.palette.species_sha256 = AshEngine::vegetation_sha256(
			fixture.bytes.data(), fixture.bytes.size());
		fixture.palette.species_asset_path = asset_path;
		return fixture;
	}

	AshEngine::VegetationChunk MakeSingleSpeciesObjectChunk(
		const AshEngine::VegetationChunkCoord coord,
		const uint8_t input_digest_byte,
		const AshEngine::VegetationPaletteEntry& palette,
		const uint16_t candidate_ordinal)
	{
		AshEngine::VegetationChunk chunk = VegetationTest::ResolvedMinimalChunk();
		chunk.chunk = coord;
		chunk.chunk_input_sha256.fill(input_digest_byte);
		chunk.species = { palette };
		chunk.instances[0].candidate_ordinal = candidate_ordinal;
		return chunk;
	}

	AshEngine::VegetationChunk MakeDualSpeciesObjectChunk(
		const ResolvedSpeciesAssetFixture& first,
		const ResolvedSpeciesAssetFixture& second,
		const AshEngine::VegetationChunkCoord coord,
		const uint8_t input_digest_byte,
		const uint16_t second_candidate_ordinal)
	{
		AshEngine::VegetationChunk chunk = MakeSingleSpeciesObjectChunk(
			coord, input_digest_byte, first.palette, 0);
		chunk.species.push_back(second.palette);
		AshEngine::VegetationChunkInstance second_instance = chunk.instances[0];
		second_instance.species_index = 1;
		second_instance.cell_x = 18;
		second_instance.candidate_ordinal = second_candidate_ordinal;
		chunk.instances.push_back(second_instance);
		return chunk;
	}

	struct MultiObjectActiveChunkSetFixture
	{
		EmptyActiveChunkSetFixture chunk_set{};
		std::vector<AshEngine::VegetationChunk> object_chunks{};
		std::vector<std::vector<uint8_t>> object_bytes{};
		std::vector<std::filesystem::path> object_relative_paths{};
		std::vector<ResolvedSpeciesAssetFixture> species_assets{};

		MultiObjectActiveChunkSetFixture(
			std::vector<AshEngine::VegetationChunk> chunks,
			std::vector<ResolvedSpeciesAssetFixture> assets)
			: object_chunks(std::move(chunks)), species_assets(std::move(assets))
		{
			if (object_chunks.empty())
			{
				throw std::runtime_error("Multi-object fixture requires at least one object");
			}
			AshEngine::VegetationChunkSetManifest manifest{};
			manifest.layer_id = object_chunks.front().layer_id;
			manifest.layer_generation = 7;
			manifest.surface_identity = object_chunks.front().surface_identity;
			std::string error{};
			for (const AshEngine::VegetationChunk& chunk : object_chunks)
			{
				std::vector<uint8_t> bytes{};
				if (!AshEngine::encode_vegetation_chunk(chunk, bytes, &error))
				{
					throw std::runtime_error("Multi-object chunk fixture failed to encode: " + error);
				}
				AshEngine::VegetationChunkSetManifestEntry entry{};
				entry.coord = chunk.chunk;
				entry.object_sha256 = AshEngine::vegetation_sha256(
					bytes.data(), bytes.size());
				entry.input_sha256 = chunk.chunk_input_sha256;
				manifest.entries.push_back(entry);
				object_bytes.push_back(std::move(bytes));
			}
			if (!AshEngine::encode_vegetation_chunk_set_manifest(
				manifest, chunk_set.manifest_bytes, &error))
			{
				throw std::runtime_error("Multi-object manifest fixture failed to encode: " + error);
			}
			chunk_set.RefreshManifestPointer();
			for (const AshEngine::VegetationChunkSetManifestEntry& entry : manifest.entries)
			{
				object_relative_paths.push_back(
					chunk_set.store_relative_path / "objects" /
					(VegetationTest::ToHex(entry.object_sha256) +
						".AshVegetationChunk"));
			}
		}

		void Write(VegetationTest::ScopedAssetRoot& root) const
		{
			Write(root, object_bytes.size(), species_assets.size());
		}

		void Write(
			VegetationTest::ScopedAssetRoot& root,
			const size_t object_count,
			const size_t species_count) const
		{
			if (object_count > object_bytes.size() || species_count > species_assets.size())
			{
				throw std::runtime_error("Multi-object fixture write count is invalid");
			}
			chunk_set.Write(root);
			for (size_t index = 0; index < object_count; ++index)
			{
				chunk_set.WriteAsset(root, object_relative_paths[index], object_bytes[index]);
			}
			for (size_t index = 0; index < species_count; ++index)
			{
				chunk_set.WriteAsset(root,
					species_assets[index].palette.species_asset_path,
					species_assets[index].bytes);
			}
		}

		uint64_t TotalInspectedBytes() const
		{
			uint64_t total = chunk_set.active_bytes.size() +
				chunk_set.manifest_bytes.size();
			for (const std::vector<uint8_t>& bytes : object_bytes)
			{
				total += bytes.size();
			}
			return total;
		}
	};

	MultiObjectActiveChunkSetFixture DistinctSpeciesTwoObjectFixture()
	{
		const ResolvedSpeciesAssetFixture first = MakeResolvedSpeciesAsset(
			VegetationTest::ResolvedMinimalPaletteEntry().species_id,
			"vegetation/MultiObjectSpeciesA.AshVegetation", 8);
		const ResolvedSpeciesAssetFixture second = MakeResolvedSpeciesAsset(
			VegetationTest::SequentialId(0x80),
			"vegetation/MultiObjectSpeciesB.AshVegetation", 8);
		return MultiObjectActiveChunkSetFixture(
			{
				MakeSingleSpeciesObjectChunk({ -2, 3 }, 0x5a, first.palette, 5),
				MakeDualSpeciesObjectChunk(
					first, second, { 4, 3 }, 0x6b, 7)
			},
			{ first, second });
	}

	MultiObjectActiveChunkSetFixture DualSpeciesSingleObjectFixture(
		const uint16_t second_candidate_ordinal = 7)
	{
		const ResolvedSpeciesAssetFixture first = MakeResolvedSpeciesAsset(
			VegetationTest::ResolvedMinimalPaletteEntry().species_id,
			"vegetation/DualSpeciesA.AshVegetation", 1);
		const ResolvedSpeciesAssetFixture second = MakeResolvedSpeciesAsset(
			VegetationTest::SequentialId(0x80),
			"vegetation/DualSpeciesB.AshVegetation", 8);
		return MultiObjectActiveChunkSetFixture(
			{ MakeDualSpeciesObjectChunk(
				first, second, { -2, 3 }, 0x5a, second_candidate_ordinal) },
			{ first, second });
	}

	AshEngine::VegetationChunkSetLoadBudget ObjectFixtureBudget(
		const MultiObjectActiveChunkSetFixture& fixture,
		const uint64_t max_summary_bytes)
	{
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.per_file = { 701, 701, 4096, 2, 0, 2 };
		budget.max_manifest_entries = static_cast<uint32_t>(
			fixture.object_chunks.size());
		budget.max_total_inspected_bytes = fixture.TotalInspectedBytes();
		budget.max_summary_bytes = max_summary_bytes;
		return budget;
	}

	AshEngine::VegetationChunkSetLoadBudget EmptyChunkSetBudget()
	{
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.per_file = VegetationTest::GenerousLoadBudget();
		budget.per_file.max_file_bytes = 96;
		budget.max_manifest_entries = 0;
		budget.max_total_inspected_bytes = 144;
		budget.max_summary_bytes = 112;
		return budget;
	}

	AshEngine::VegetationChunkSetLoadBudget SingleObjectChunkSetBudget()
	{
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.per_file = { 701, 701, 232, 1, 0, 1 };
		budget.max_manifest_entries = 1;
		budget.max_total_inspected_bytes = 508;
		budget.max_summary_bytes = 216;
		return budget;
	}

	std::shared_ptr<const AshEngine::VegetationAssetResolverSnapshot>
	CaptureFixtureResolver(const VegetationTest::ScopedAssetRoot& root)
	{
		AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
		if (!database.is_valid() || !database.refresh())
		{
			throw std::runtime_error("Single-object fixture AssetDatabase refresh failed");
		}
		auto resolver = database.capture_vegetation_resolver_snapshot();
		if (!resolver)
		{
			throw std::runtime_error("Single-object fixture resolver capture failed");
		}
		return resolver;
	}

	class RecordingChunkSetReadFileOps final : public AshEngine::IVegetationStageFileOps
	{
	public:
		struct ReadCall
		{
			std::filesystem::path path{};
			uint64_t max_bytes = 0;
		};

		explicit RecordingChunkSetReadFileOps(AshEngine::IVegetationStageFileOps& backing)
			: m_backing(backing)
		{
		}

		std::vector<std::filesystem::path> inspected_paths{};
		std::vector<ReadCall> read_calls{};
		std::shared_ptr<std::atomic_bool> cancellation{};
		size_t cancel_after_successful_inspection = 0;
		size_t cancel_after_successful_read = 0;
		size_t illegal_bytes_on_read = 0;
		size_t overridden_inspection_call = 0;
		AshEngine::VegetationFileInspection inspection_override{};
		size_t overridden_read_call = 0;
		AshEngine::VegetationFileBytesResult read_override{};
		size_t throw_inspection_call = 0;
		size_t throw_read_call = 0;
		size_t mutation_call_count = 0;

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			inspected_paths.push_back(path);
			if (throw_inspection_call == inspected_paths.size())
			{
				throw std::runtime_error("injected active-read inspection exception");
			}
			AshEngine::VegetationFileInspection result{};
			if (overridden_inspection_call == inspected_paths.size())
			{
				result = inspection_override;
			}
			else
			{
				result = m_backing.InspectPath(asset_root, path);
			}
			if (result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				cancel_after_successful_inspection == inspected_paths.size() && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			read_calls.push_back({ path, max_bytes });
			if (throw_read_call == read_calls.size())
			{
				throw std::runtime_error("injected active-read byte exception");
			}
			if (overridden_read_call == read_calls.size())
			{
				return read_override;
			}
			if (illegal_bytes_on_read == read_calls.size())
			{
				AshEngine::VegetationFileBytesResult result{};
				result.status = AshEngine::VegetationFileResultStatus::Failed;
				result.bytes = { 0x55 };
				result.error = "injected non-success byte result carrying payload";
				return result;
			}
			AshEngine::VegetationFileBytesResult result =
				m_backing.ReadAllBytes(path, max_bytes);
			if (result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				cancel_after_successful_read == read_calls.size() && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			++mutation_call_count;
			return m_backing.EnsureDirectoryTree(asset_root, relative_directory);
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			++mutation_call_count;
			return m_backing.CreateUniqueSiblingStageFile(target, operation_serial);
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			++mutation_call_count;
			return m_backing.CreateUniqueStageTree(store_root, operation_serial);
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			++mutation_call_count;
			return m_backing.CreateOwnedStageFile(owned_stage_root, relative_path);
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			++mutation_call_count;
			return m_backing.RemoveOwnedStageFile(
				stage_file, expected_identity);
		}

		bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			++mutation_call_count;
			return m_backing.RemoveOwnedStageTree(stage_root, expected_identity);
		}

	private:
		AshEngine::IVegetationStageFileOps& m_backing;
	};

	struct PrepareFileOpEvent
	{
		std::string name{};
		std::filesystem::path path{};
		std::filesystem::path auxiliary_path{};
		uint64_t value = 0;
		uint64_t offset = 0;
	};

	enum class PrepareFaultMode : uint8_t
	{
		ReturnFailed = 0,
		ReturnInvalidPath,
		IllegalPayload,
		ReturnFalse,
		CorruptBytes,
		Throw,
		CancelAfterSuccess
	};

	struct PrepareFaultRule
	{
		std::string event{};
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFailed;
	};

	std::optional<PrepareFaultMode> MatchPrepareFault(
		const std::vector<PrepareFaultRule>& rules,
		std::vector<std::string>& observed_events,
		const std::string_view event)
	{
		observed_events.emplace_back(event);
		const size_t occurrence = static_cast<size_t>(std::count(
			observed_events.begin(), observed_events.end(), event));
		const auto found = std::find_if(rules.begin(), rules.end(),
			[&](const PrepareFaultRule& rule)
			{
				return rule.event == event && rule.occurrence == occurrence;
			});
		return found == rules.end()
			? std::nullopt
			: std::optional<PrepareFaultMode>(found->mode);
	}

	class ScopedCurrentPathRestore final
	{
	public:
		ScopedCurrentPathRestore()
			: m_original(std::filesystem::current_path())
		{
		}

		~ScopedCurrentPathRestore()
		{
			std::error_code ignored{};
			std::filesystem::current_path(m_original, ignored);
		}

		void Set(const std::filesystem::path& path)
		{
			std::filesystem::current_path(path);
		}

	private:
		std::filesystem::path m_original{};
	};

	void WritePrepareBytes(
		const std::filesystem::path& path,
		const std::vector<uint8_t>& bytes)
	{
#if defined(_WIN32)
		std::filesystem::path preferred = path;
		preferred.make_preferred();
		const std::wstring extended = L"\\\\?\\" + preferred.native();
		const HANDLE file = CreateFileW(
			extended.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			throw std::runtime_error("Failed to open prepare fixture path");
		}
		DWORD written = 0;
		const bool wrote = bytes.size() <= std::numeric_limits<DWORD>::max() &&
			WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
				&written, nullptr) != FALSE;
		const bool closed = CloseHandle(file) != FALSE;
		if (!wrote || written != bytes.size() || !closed)
		{
			throw std::runtime_error("Failed to write prepare fixture path");
		}
#else
		VegetationTest::WriteAllBytes(path, bytes);
#endif
	}

#if defined(_WIN32)
	std::wstring ExtendedPrepareWindowsPath(const std::filesystem::path& path)
	{
		std::filesystem::path preferred = path;
		preferred.make_preferred();
		return L"\\\\?\\" + preferred.native();
	}
#endif

	class RecordingPrepareStageWriter final :
		public AshEngine::IVegetationStageFileWriter
	{
	public:
		RecordingPrepareStageWriter(
			std::unique_ptr<AshEngine::IVegetationStageFileWriter> backing,
			std::vector<PrepareFileOpEvent>& events,
			const std::vector<PrepareFaultRule>& fault_rules,
			std::vector<std::string>& observed_fault_events,
			std::shared_ptr<std::atomic_bool>& cancellation,
			std::string label,
			std::filesystem::path path)
			: m_backing(std::move(backing)), m_events(events),
			  m_fault_rules(fault_rules),
			  m_observed_fault_events(observed_fault_events),
			  m_cancellation(cancellation), m_label(std::move(label)),
			  m_path(std::move(path))
		{
		}

		bool WriteBlock(
			const uint64_t offset,
			const AshEngine::VegetationByteSpan bytes) override
		{
			const std::string event = m_label + ".write";
			m_events.push_back({ event, m_path, {}, bytes.size, offset });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				m_fault_rules, m_observed_fault_events, event);
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare stage-write failure");
			}
			if (fault == PrepareFaultMode::ReturnFalse ||
				fault == PrepareFaultMode::ReturnFailed)
			{
				return false;
			}
			const bool result = m_backing->WriteBlock(offset, bytes);
			if (result && fault == PrepareFaultMode::CancelAfterSuccess &&
				m_cancellation)
			{
				m_cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		bool FlushAndClose() override
		{
			const std::string event = m_label + ".flush";
			m_events.push_back({ event, m_path });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				m_fault_rules, m_observed_fault_events, event);
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare stage-flush failure");
			}
			if (fault == PrepareFaultMode::ReturnFalse ||
				fault == PrepareFaultMode::ReturnFailed)
			{
				return false;
			}
			const bool result = m_backing->FlushAndClose();
			if (result && fault == PrepareFaultMode::CancelAfterSuccess &&
				m_cancellation)
			{
				m_cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

	private:
		std::unique_ptr<AshEngine::IVegetationStageFileWriter> m_backing{};
		std::vector<PrepareFileOpEvent>& m_events;
		const std::vector<PrepareFaultRule>& m_fault_rules;
		std::vector<std::string>& m_observed_fault_events;
		std::shared_ptr<std::atomic_bool>& m_cancellation;
		std::string m_label{};
		std::filesystem::path m_path{};
	};

	class RecordingCommitLease final : public AshEngine::IVegetationFileLease
	{
	public:
		explicit RecordingCommitLease(std::shared_ptr<size_t> destruction_count)
			: m_destruction_count(std::move(destruction_count))
		{
		}

		~RecordingCommitLease() override
		{
			++*m_destruction_count;
		}

	private:
		std::shared_ptr<size_t> m_destruction_count{};
	};

	class RecordingImmutablePublishFileOps final :
		public AshEngine::IVegetationFileOps
	{
	public:
		explicit RecordingImmutablePublishFileOps(
			AshEngine::IVegetationFileOps& backing)
			: m_backing(backing)
		{
		}

		std::vector<PrepareFileOpEvent> events{};
		std::vector<PrepareFaultRule> fault_rules{};
		std::vector<std::string> observed_fault_events{};
		std::function<void(std::string_view, size_t)> after_success{};
		std::vector<std::filesystem::path> root_arguments{};
		std::vector<std::filesystem::path> child_stage_paths{};
		std::filesystem::path stage_tree{};
		AshEngine::VegetationFileIdentity stage_tree_identity{};
		std::filesystem::path sibling_stage{};
		AshEngine::VegetationFileIdentity sibling_identity{};
		std::vector<AshEngine::VegetationCreateNewStatus> publish_results{};
		size_t fail_publish_call = 0;
		size_t corrupt_after_created_publish_call = 0;
		bool fail_remove_tree = false;
		bool use_stage_tree_override = false;
		AshEngine::VegetationStageTreeResult stage_tree_override{};
		size_t overridden_read_call = 0;
		AshEngine::VegetationFileBytesResult read_override{};
		size_t rewrite_resolved_inspection_call = 0;
		std::filesystem::path rewritten_resolved_path{};
		std::filesystem::path rewrite_identity_inspection_path{};
		size_t rewrite_identity_path_occurrence = 0;
		size_t rewrite_identity_path_count = 0;
		AshEngine::VegetationFileIdentity rewritten_file_identity{};
		size_t inspection_call_count = 0;
		size_t throw_inspection_call = 0;
		size_t alias_active_to_sibling_inspection_call = 0;
		size_t change_cwd_after_inspection_call = 0;
		std::filesystem::path changed_cwd{};
		std::shared_ptr<std::atomic_bool> cancellation{};
		size_t cancel_after_inspection_call = 0;
		size_t cancel_after_read_call = 0;
		size_t read_call_count = 0;
		std::function<void(const std::filesystem::path&,
			AshEngine::VegetationFileInspection&)> inspection_result_hook{};
		std::function<void(const std::filesystem::path&, uint64_t,
			AshEngine::VegetationFileBytesResult&)> read_result_hook{};
		AshEngine::VegetationFileLeaseStatus lease_status =
			AshEngine::VegetationFileLeaseStatus::Acquired;
		bool throw_acquire = false;
		bool acquired_lease_without_payload = false;
		bool non_acquired_lease_with_payload = false;
		std::function<void()> after_lease_result{};
		std::function<AshEngine::VegetationCreateNewStatus(
			const std::filesystem::path&, const std::filesystem::path&)> create_new{};
		std::function<AshEngine::VegetationAtomicReplaceResult(
			const std::filesystem::path&, const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)> atomic_replace{};
		size_t acquire_call_count = 0;
		size_t atomic_replace_call_count = 0;
		size_t create_new_call_count = 0;
		std::string last_lease_identity{};
		std::shared_ptr<const std::atomic_bool> last_lease_cancel_requested{};
		std::chrono::steady_clock::time_point last_lease_deadline{};
		std::filesystem::path last_create_stage{};
		std::filesystem::path last_create_target{};
		std::filesystem::path last_atomic_stage{};
		std::filesystem::path last_atomic_target{};
		std::shared_ptr<size_t> lease_destruction_count =
			std::make_shared<size_t>(0);

		std::vector<std::string> EventNames() const
		{
			std::vector<std::string> names{};
			names.reserve(events.size());
			for (const PrepareFileOpEvent& event : events)
			{
				names.push_back(event.name);
			}
			return names;
		}

		bool HasEvent(const std::string_view name) const
		{
			return std::any_of(events.begin(), events.end(),
				[&](const PrepareFileOpEvent& event)
				{
					return event.name == name;
				});
		}

		size_t EventCount(const std::string_view name) const
		{
			return static_cast<size_t>(std::count_if(events.begin(), events.end(),
				[&](const PrepareFileOpEvent& event)
				{
					return event.name == name;
				}));
		}

		bool HasMutationEvent() const
		{
			return std::any_of(events.begin(), events.end(),
				[](const PrepareFileOpEvent& event)
				{
					return event.name != "inspect" && event.name != "read";
				});
		}

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			root_arguments.push_back(asset_root);
			events.push_back({ "inspect", path });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "inspect");
			++inspection_call_count;
			if (fault == PrepareFaultMode::Throw ||
				throw_inspection_call == inspection_call_count)
			{
				throw std::runtime_error("injected prepare inspection failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return {};
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				AshEngine::VegetationFileInspection result{};
				result.status = AshEngine::VegetationFileResultStatus::InvalidPath;
				return result;
			}
			AshEngine::VegetationFileInspection result =
				m_backing.InspectPath(asset_root, path);
			if (rewrite_resolved_inspection_call == inspection_call_count)
			{
				result.resolved_absolute_path = rewritten_resolved_path;
			}
			if (!rewrite_identity_inspection_path.empty() &&
				path == rewrite_identity_inspection_path)
			{
				++rewrite_identity_path_count;
				if (rewrite_identity_path_count == rewrite_identity_path_occurrence)
				{
					result.file_identity = rewritten_file_identity;
				}
			}
			if (alias_active_to_sibling_inspection_call == inspection_call_count)
			{
				result.status = AshEngine::VegetationFileResultStatus::Succeeded;
				result.exists = true;
				result.is_regular_file = true;
				result.file_identity = sibling_identity;
				result.error.clear();
			}
			if (inspection_result_hook)
			{
				inspection_result_hook(path, result);
			}
			if (change_cwd_after_inspection_call == inspection_call_count)
			{
				std::filesystem::current_path(changed_cwd);
			}
			if (fault == PrepareFaultMode::IllegalPayload)
			{
				result.status = result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded
						? AshEngine::VegetationFileResultStatus::Failed
						: AshEngine::VegetationFileResultStatus::Succeeded;
			}
			if ((fault == PrepareFaultMode::CancelAfterSuccess ||
					cancel_after_inspection_call == inspection_call_count) &&
				result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			events.push_back({ "read", path, {}, max_bytes });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "read");
			++read_call_count;
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare read failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return {};
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				AshEngine::VegetationFileBytesResult result{};
				result.status = AshEngine::VegetationFileResultStatus::InvalidPath;
				return result;
			}
			AshEngine::VegetationFileBytesResult result =
				overridden_read_call == read_call_count
					? read_override
					: m_backing.ReadAllBytes(path, max_bytes);
			if (fault == PrepareFaultMode::IllegalPayload)
			{
				result.status = result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded
						? AshEngine::VegetationFileResultStatus::Failed
						: AshEngine::VegetationFileResultStatus::Succeeded;
			}
			if (fault == PrepareFaultMode::CorruptBytes && !result.bytes.empty())
			{
				result.bytes.back() ^= 0xffu;
			}
			if (read_result_hook)
			{
				read_result_hook(path, max_bytes, result);
			}
			if ((fault == PrepareFaultMode::CancelAfterSuccess ||
					cancel_after_read_call == read_call_count) &&
				result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			root_arguments.push_back(asset_root);
			events.push_back({ "ensure", relative_directory });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "ensure");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare ensure failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return AshEngine::VegetationFileResultStatus::Failed;
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				return AshEngine::VegetationFileResultStatus::InvalidPath;
			}
			const AshEngine::VegetationFileResultStatus result =
				m_backing.EnsureDirectoryTree(asset_root, relative_directory);
			if (fault == PrepareFaultMode::CancelAfterSuccess && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			events.push_back({ "create-sibling", target, {}, operation_serial });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "create-sibling");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare sibling-stage failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return {};
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				AshEngine::VegetationStageFileResult result{};
				result.status = AshEngine::VegetationFileResultStatus::InvalidPath;
				return result;
			}
			AshEngine::VegetationStageFileResult result =
				m_backing.CreateUniqueSiblingStageFile(target, operation_serial);
			if (result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				result.writer)
			{
				sibling_stage = result.owned_stage_file;
				sibling_identity = result.file_identity;
				result.writer = std::make_unique<RecordingPrepareStageWriter>(
					std::move(result.writer), events, fault_rules,
					observed_fault_events, cancellation, "active",
					result.owned_stage_file);
			}
			const bool backing_succeeded = result.status ==
				AshEngine::VegetationFileResultStatus::Succeeded;
			if (backing_succeeded && after_success)
			{
				after_success("create-sibling", static_cast<size_t>(std::count(
					observed_fault_events.begin(), observed_fault_events.end(),
					"create-sibling")));
			}
			if (fault == PrepareFaultMode::IllegalPayload)
			{
				result.status = result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded
						? AshEngine::VegetationFileResultStatus::Failed
						: AshEngine::VegetationFileResultStatus::Succeeded;
			}
			if (fault == PrepareFaultMode::CancelAfterSuccess && cancellation &&
				backing_succeeded)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			events.push_back({ "create-tree", store_root, {}, operation_serial });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "create-tree");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare stage-tree failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return {};
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				AshEngine::VegetationStageTreeResult result{};
				result.status = AshEngine::VegetationFileResultStatus::InvalidPath;
				return result;
			}
			AshEngine::VegetationStageTreeResult result = use_stage_tree_override
				? stage_tree_override
				: m_backing.CreateUniqueStageTree(store_root, operation_serial);
			if (!result.owned_stage_root.empty())
			{
				stage_tree = result.owned_stage_root;
				stage_tree_identity = result.file_identity;
			}
			if (fault == PrepareFaultMode::IllegalPayload)
			{
				result.status = result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded
						? AshEngine::VegetationFileResultStatus::Failed
						: AshEngine::VegetationFileResultStatus::Succeeded;
			}
			if (fault == PrepareFaultMode::CancelAfterSuccess && cancellation &&
				result.status == AshEngine::VegetationFileResultStatus::Succeeded)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			events.push_back({ "create-child", relative_path, owned_stage_root });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "create-child");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare child-stage failure");
			}
			if (fault == PrepareFaultMode::ReturnFailed)
			{
				return {};
			}
			if (fault == PrepareFaultMode::ReturnInvalidPath)
			{
				AshEngine::VegetationStageFileResult result{};
				result.status = AshEngine::VegetationFileResultStatus::InvalidPath;
				return result;
			}
			AshEngine::VegetationStageFileResult result =
				m_backing.CreateOwnedStageFile(owned_stage_root, relative_path);
			if (result.status == AshEngine::VegetationFileResultStatus::Succeeded &&
				result.writer)
			{
				child_stage_paths.push_back(result.owned_stage_file);
				const std::string label = relative_path.filename() ==
					std::filesystem::path(L".ashveg-layer-stage-manifest.tmp")
						? "manifest" : "object";
				result.writer = std::make_unique<RecordingPrepareStageWriter>(
					std::move(result.writer), events, fault_rules,
					observed_fault_events, cancellation, label,
					result.owned_stage_file);
			}
			if (fault == PrepareFaultMode::IllegalPayload)
			{
				result.status = result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded
						? AshEngine::VegetationFileResultStatus::Failed
						: AshEngine::VegetationFileResultStatus::Succeeded;
			}
			if (fault == PrepareFaultMode::CancelAfterSuccess && cancellation &&
				result.status == AshEngine::VegetationFileResultStatus::Succeeded)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			events.push_back({ "remove-file", stage_file });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "remove-file");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare stage-file cleanup failure");
			}
			if (fault == PrepareFaultMode::ReturnFalse ||
				fault == PrepareFaultMode::ReturnFailed)
			{
				return false;
			}
			return m_backing.RemoveOwnedStageFile(
				stage_file, expected_identity);
		}

		bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			events.push_back({ "remove-tree", stage_root });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "remove-tree");
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare stage-tree cleanup failure");
			}
			if (fail_remove_tree || fault == PrepareFaultMode::ReturnFalse ||
				fault == PrepareFaultMode::ReturnFailed)
			{
				return false;
			}
			return m_backing.RemoveOwnedStageTree(stage_root, expected_identity);
		}

		AshEngine::VegetationCreateNewStatus PublishImmutableFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& content_addressed_target) override
		{
			events.push_back({ "publish", content_addressed_target, stage });
			const std::optional<PrepareFaultMode> fault = MatchPrepareFault(
				fault_rules, observed_fault_events, "publish");
			const size_t call = publish_results.size() + 1;
			if (fault == PrepareFaultMode::Throw)
			{
				throw std::runtime_error("injected prepare immutable publish failure");
			}
			AshEngine::VegetationCreateNewStatus result =
				AshEngine::VegetationCreateNewStatus::Failed;
			if (fail_publish_call != call &&
				fault != PrepareFaultMode::ReturnFailed &&
				fault != PrepareFaultMode::ReturnFalse)
			{
				result = m_backing.PublishImmutableFromStage(
					stage, content_addressed_target);
			}
			publish_results.push_back(result);
			if (result == AshEngine::VegetationCreateNewStatus::Created &&
				(corrupt_after_created_publish_call == call ||
					fault == PrepareFaultMode::CorruptBytes))
			{
				AshEngine::VegetationFileBytesResult bytes = m_backing.ReadAllBytes(
					content_addressed_target, std::numeric_limits<uint64_t>::max());
				if (bytes.status != AshEngine::VegetationFileResultStatus::Succeeded ||
					bytes.bytes.empty())
				{
					throw std::runtime_error("Could not read immutable target for corruption");
				}
				bytes.bytes[0] ^= 0xffu;
				WritePrepareBytes(content_addressed_target, bytes.bytes);
			}
			if ((result == AshEngine::VegetationCreateNewStatus::Created ||
				result == AshEngine::VegetationCreateNewStatus::AlreadyExists) &&
				fault == PrepareFaultMode::CancelAfterSuccess && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileLeaseResult AcquireNamedLease(
			const std::string_view canonical_identity,
			const AshEngine::VegetationOperationControl& control) override
		{
			++acquire_call_count;
			last_lease_identity.assign(canonical_identity.begin(), canonical_identity.end());
			last_lease_cancel_requested = control.cancel_requested;
			last_lease_deadline = control.deadline;
			if (throw_acquire)
			{
				throw std::runtime_error("injected commit lease exception");
			}
			AshEngine::VegetationFileLeaseResult result{};
			result.status = lease_status;
			if ((lease_status == AshEngine::VegetationFileLeaseStatus::Acquired &&
					!acquired_lease_without_payload) ||
				(lease_status != AshEngine::VegetationFileLeaseStatus::Acquired &&
					non_acquired_lease_with_payload))
			{
				result.lease = std::make_unique<RecordingCommitLease>(
					lease_destruction_count);
			}
			if (after_lease_result)
			{
				after_lease_result();
			}
			return result;
		}

		AshEngine::VegetationAtomicReplaceResult AtomicReplace(
			const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry) override
		{
			++atomic_replace_call_count;
			last_atomic_stage = stage;
			last_atomic_target = target;
			if (atomic_replace)
			{
				return atomic_replace(stage, target, cleanup_registry);
			}
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::TargetPreserved;
			return result;
		}

		AshEngine::VegetationCreateNewStatus CreateNewFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) override
		{
			++create_new_call_count;
			last_create_stage = stage;
			last_create_target = target;
			return create_new ? create_new(stage, target) :
				m_backing.CreateNewFromStage(stage, target);
		}

	private:
		AshEngine::IVegetationFileOps& m_backing;
	};

	std::vector<uint8_t> EncodeLayerOrThrow(
		const AshEngine::VegetationLayerSnapshot& layer)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(layer, bytes, &error))
		{
			throw std::runtime_error("Prepare fixture Layer encode failed: " + error);
		}
		return bytes;
	}

	struct NoActivePrepareFixture
	{
		std::filesystem::path layer_relative_path =
			"vegetation/meadow.AshVegetationLayer";
		std::filesystem::path store_relative_path{};
		std::filesystem::path object_relative_path{};
		std::vector<std::filesystem::path> object_relative_paths{};
		std::filesystem::path manifest_relative_path{};
		std::filesystem::path active_relative_path{};
		AshEngine::VegetationBakeInput input{};
		AshEngine::VegetationBakeResult baked{};
		std::vector<uint8_t> manifest_bytes{};
		AshEngine::VegetationSha256 manifest_sha256{};

		NoActivePrepareFixture()
			: NoActivePrepareFixture(SingleChunkBakeInput(0x1234u, 7))
		{
		}

		explicit NoActivePrepareFixture(AshEngine::VegetationBakeInput bake_input)
			: input(std::move(bake_input))
		{
			store_relative_path = layer_relative_path;
			store_relative_path += ".AshVegetationChunks";
			active_relative_path = store_relative_path / "active.asva";
			baked = AshEngine::bake_vegetation_chunks(
				input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
			RefreshArtifacts();
		}

		void RefreshArtifacts()
		{
			const AshEngine::VegetationBakeTransactionOutput& transaction =
				RequireTransaction(baked);
			if (transaction.chunks.empty())
			{
				throw std::runtime_error("No-active prepare fixture requires a chunk");
			}
			std::string error{};
			manifest_bytes.clear();
			if (!AshEngine::encode_vegetation_chunk_set_manifest(
				transaction.resulting_manifest, manifest_bytes, &error))
			{
				throw std::runtime_error("No-active prepare manifest failed: " + error);
			}
			manifest_sha256 = AshEngine::vegetation_sha256(
				manifest_bytes.data(), manifest_bytes.size());
			object_relative_paths.clear();
			object_relative_paths.reserve(transaction.chunks.size());
			for (const AshEngine::VegetationBakedChunk& chunk : transaction.chunks)
			{
				object_relative_paths.push_back(store_relative_path / "objects" /
					(VegetationTest::ToHex(chunk.object_sha256) +
						".AshVegetationChunk"));
			}
			object_relative_path = object_relative_paths.front();
			manifest_relative_path = store_relative_path / "manifests" /
				(VegetationTest::ToHex(manifest_sha256) + ".asvm");
		}

		const AshEngine::VegetationBakeTransactionOutput& Transaction() const
		{
			return RequireTransaction(baked);
		}

		void WriteLayer(VegetationTest::ScopedAssetRoot& root) const
		{
			root.Write(layer_relative_path, EncodeLayerOrThrow(*input.layer_snapshot));
		}

		void WriteAsset(
			VegetationTest::ScopedAssetRoot& root,
			const std::filesystem::path& relative_path,
			const std::vector<uint8_t>& bytes) const
		{
			const std::filesystem::path absolute = (root.Path() / relative_path).lexically_normal();
			if (AshEngine::get_default_vegetation_file_ops().EnsureDirectoryTree(
					root.Path(), relative_path.parent_path()) !=
				AshEngine::VegetationFileResultStatus::Succeeded)
			{
				throw std::runtime_error("Failed to create prepare fixture parent");
			}
			WritePrepareBytes(absolute, bytes);
		}
	};

	std::vector<uint8_t> EncodeActivePointerOrThrow(
		const AshEngine::VegetationSha256& manifest_sha256)
	{
		AshEngine::VegetationChunkSetActivePointer pointer{};
		pointer.manifest_sha256 = manifest_sha256;
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_chunk_set_active_pointer(
				pointer, bytes, &error))
		{
			throw std::runtime_error("Commit fixture active encode failed: " + error);
		}
		return bytes;
	}

	struct NoActiveCommitFixture
	{
		VegetationTest::ScopedAssetRoot root;
		NoActivePrepareFixture source{};
		RecordingImmutablePublishFileOps file_ops;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		std::shared_ptr<std::atomic_bool> cancellation =
			std::make_shared<std::atomic_bool>(false);
		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};

		explicit NoActiveCommitFixture(const std::string& label)
			: root(label),
			file_ops(AshEngine::get_default_vegetation_file_ops())
		{
			source.WriteLayer(root);
			file_ops.cancellation = cancellation;
			prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), source.layer_relative_path, source.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(5)),
				cleanup_registry, file_ops));
			if (prepared->status() != AshEngine::VegetationChunkSetPrepareStatus::Prepared)
			{
				throw std::runtime_error(
					"No-active commit fixture preparation failed: " + prepared->error());
			}
			ResetCommitObservations();
		}

		void ResetCommitObservations()
		{
			file_ops.events.clear();
			file_ops.observed_fault_events.clear();
			file_ops.root_arguments.clear();
			file_ops.inspection_call_count = 0;
			file_ops.read_call_count = 0;
			file_ops.rewrite_identity_path_count = 0;
			file_ops.acquire_call_count = 0;
			file_ops.atomic_replace_call_count = 0;
			file_ops.create_new_call_count = 0;
			file_ops.last_lease_identity.clear();
			file_ops.last_lease_cancel_requested.reset();
			file_ops.last_lease_deadline = {};
			file_ops.last_create_stage.clear();
			file_ops.last_create_target.clear();
			*file_ops.lease_destruction_count = 0;
		}

		AshEngine::VegetationOperationControl Control() const
		{
			AshEngine::VegetationOperationControl control{};
			control.cancel_requested = cancellation;
			control.deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(5);
			return control;
		}

		const AshEngine::VegetationChunkSetExpectedIdentity& Expected() const
		{
			return source.Transaction().expected_identity;
		}

		std::filesystem::path ActiveAbsolute() const
		{
			return (std::filesystem::absolute(root.Path()).lexically_normal() /
				source.active_relative_path).lexically_normal();
		}

		AshEngine::VegetationChunkSetCommitResult Commit(
			const AshEngine::VegetationChunkSetExpectedIdentity& current_identity,
			AshEngine::VegetationOperationControl control)
		{
			return AshEngine::commit_vegetation_chunk_set(
				*prepared, current_identity, std::move(control),
				cleanup_registry, file_ops);
		}

		AshEngine::VegetationChunkSetCommitResult Commit()
		{
			return Commit(Expected(), Control());
		}
	};

	void ExpandPrepareFixtureFirstObject(
		NoActivePrepareFixture& fixture,
		const size_t instance_count)
	{
		AshEngine::VegetationBakeTransactionOutput& transaction =
			*fixture.baked.transaction;
		AshEngine::VegetationBakedChunk& baked = transaction.chunks.front();
		constexpr size_t cell_axis = 256u;
		constexpr size_t candidate_axis = 256u;
		constexpr size_t max_canonical_instances =
			cell_axis * cell_axis * candidate_axis;
		if (baked.chunk.instances.empty() || instance_count == 0 ||
			instance_count > max_canonical_instances)
		{
			throw std::runtime_error("Large prepare fixture instance count is invalid");
		}
		const AshEngine::VegetationChunkInstance prototype =
			baked.chunk.instances.front();
		baked.chunk.instances.clear();
		baked.chunk.instances.reserve(instance_count);
		for (size_t index = 0; index < instance_count; ++index)
		{
			AshEngine::VegetationChunkInstance instance = prototype;
			instance.species_index = 0;
			const size_t cell_index = index / candidate_axis;
			instance.cell_z = static_cast<uint16_t>(cell_index / cell_axis);
			instance.cell_x = static_cast<uint16_t>(cell_index % cell_axis);
			instance.candidate_ordinal = static_cast<uint16_t>(
				index % candidate_axis);
			baked.chunk.instances.push_back(instance);
		}
		baked.chunk.min_world_height_mm = prototype.world_height_mm;
		baked.chunk.max_world_height_mm = prototype.world_height_mm;
		std::string error{};
		if (!AshEngine::encode_vegetation_chunk(
			baked.chunk, baked.object_bytes, &error))
		{
			throw std::runtime_error("Large prepare fixture encode failed: " + error);
		}
		baked.object_sha256 = AshEngine::vegetation_sha256(
			baked.object_bytes.data(), baked.object_bytes.size());
		const auto entry = std::find_if(
			transaction.resulting_manifest.entries.begin(),
			transaction.resulting_manifest.entries.end(),
			[&](const AshEngine::VegetationChunkSetManifestEntry& candidate)
			{
				return candidate.coord.x == baked.coord.x &&
					candidate.coord.z == baked.coord.z;
			});
		if (entry == transaction.resulting_manifest.entries.end())
		{
			throw std::runtime_error("Large prepare fixture manifest entry is missing");
		}
		entry->object_sha256 = baked.object_sha256;
		fixture.RefreshArtifacts();
	}

	struct ExistingPrepareFixture
	{
		NoActivePrepareFixture no_active{};
		AshEngine::VegetationBakeResult baked{};
		AshEngine::VegetationChunkSetManifest source_manifest{};
		std::vector<uint8_t> source_manifest_bytes{};
		AshEngine::VegetationSha256 source_manifest_sha256{};
		std::filesystem::path source_manifest_relative_path{};
		std::vector<uint8_t> source_active_bytes{};
		AshEngine::VegetationChunkSetManifestEntry untouched_entry{};

		ExistingPrepareFixture()
		{
			baked = no_active.baked;
			AshEngine::VegetationBakeTransactionOutput& transaction =
				*baked.transaction;
			AshEngine::VegetationBakedChunk added = transaction.chunks.front();
			added.coord = { 2, 0 };
			added.input_digest.fill(0x92);
			added.chunk.chunk = added.coord;
			added.chunk.chunk_input_sha256 = added.input_digest;
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk(
				added.chunk, added.object_bytes, &error))
			{
				throw std::runtime_error("Existing prepare addition failed to encode");
			}
			added.object_sha256 = AshEngine::vegetation_sha256(
				added.object_bytes.data(), added.object_bytes.size());
			transaction.chunks.push_back(std::move(added));
			transaction.removed_coords = { { 1, 0 } };
			transaction.expected_identity.target_coords = {
				{ 0, 0 }, { 1, 0 }, { 2, 0 }
			};
			transaction.full_rebake_required = false;

			auto make_entry = [](const AshEngine::VegetationChunkCoord coord,
				const uint8_t object_byte, const uint8_t input_byte)
			{
				AshEngine::VegetationChunkSetManifestEntry entry{};
				entry.coord = coord;
				entry.object_sha256.fill(object_byte);
				entry.input_sha256.fill(input_byte);
				return entry;
			};
			source_manifest.layer_id = transaction.expected_identity.layer_id;
			source_manifest.layer_generation =
				transaction.expected_identity.layer_generation;
			source_manifest.surface_identity =
				transaction.expected_identity.surface_identity;
			untouched_entry = make_entry({ 3, 0 }, 0x73, 0x83);
			source_manifest.entries = {
				make_entry({ 0, 0 }, 0x71, 0x81),
				make_entry({ 1, 0 }, 0x72, 0x82),
				untouched_entry
			};

			transaction.resulting_manifest.entries.clear();
			for (const AshEngine::VegetationBakedChunk& chunk : transaction.chunks)
			{
				transaction.resulting_manifest.entries.push_back({
					chunk.coord, chunk.object_sha256, chunk.input_digest });
			}
			transaction.resulting_manifest.entries.push_back(untouched_entry);
			RefreshSourceIdentity();
		}

		const AshEngine::VegetationBakeTransactionOutput& Transaction() const
		{
			return RequireTransaction(baked);
		}

		void RefreshSourceIdentity()
		{
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk_set_manifest(
				source_manifest, source_manifest_bytes, &error))
			{
				throw std::runtime_error("Existing prepare source manifest failed to encode");
			}
			BindSourceBytes(std::move(source_manifest_bytes));
		}

		void BindSourceBytes(std::vector<uint8_t> bytes)
		{
			source_manifest_bytes = std::move(bytes);
			source_manifest_sha256 = AshEngine::vegetation_sha256(
				source_manifest_bytes.data(), source_manifest_bytes.size());
			source_manifest_relative_path = no_active.store_relative_path /
				"manifests" /
				(VegetationTest::ToHex(source_manifest_sha256) + ".asvm");
			AshEngine::VegetationChunkSetActivePointer pointer{};
			pointer.manifest_sha256 = source_manifest_sha256;
			std::string error{};
			if (!AshEngine::encode_vegetation_chunk_set_active_pointer(
				pointer, source_active_bytes, &error))
			{
				throw std::runtime_error("Existing prepare active pointer failed to encode");
			}
			baked.transaction->source_active_identity.state =
				AshEngine::VegetationChunkSetSourceActiveState::Existing;
			baked.transaction->source_active_identity.manifest_sha256 =
				source_manifest_sha256;
		}

		void WriteLayer(VegetationTest::ScopedAssetRoot& root) const
		{
			no_active.WriteLayer(root);
		}

		void WriteSourceStore(VegetationTest::ScopedAssetRoot& root) const
		{
			no_active.WriteAsset(root, source_manifest_relative_path,
				source_manifest_bytes);
			no_active.WriteAsset(root, no_active.active_relative_path,
				source_active_bytes);
		}

		void Write(VegetationTest::ScopedAssetRoot& root) const
		{
			WriteLayer(root);
			WriteSourceStore(root);
		}
	};

	struct ExistingCommitFixture
	{
		VegetationTest::ScopedAssetRoot root;
		ExistingPrepareFixture source{};
		RecordingImmutablePublishFileOps file_ops;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		std::shared_ptr<std::atomic_bool> cancellation =
			std::make_shared<std::atomic_bool>(false);
		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};

		explicit ExistingCommitFixture(const std::string& label)
			: root(label),
			file_ops(AshEngine::get_default_vegetation_file_ops())
		{
			source.Write(root);
			file_ops.cancellation = cancellation;
			prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), source.no_active.layer_relative_path, source.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(5)),
				cleanup_registry, file_ops));
			if (prepared->status() != AshEngine::VegetationChunkSetPrepareStatus::Prepared)
			{
				throw std::runtime_error(
					"Existing commit fixture preparation failed: " + prepared->error());
			}
			ResetCommitObservations();
		}

		void ResetCommitObservations()
		{
			file_ops.events.clear();
			file_ops.observed_fault_events.clear();
			file_ops.root_arguments.clear();
			file_ops.inspection_call_count = 0;
			file_ops.read_call_count = 0;
			file_ops.rewrite_identity_path_count = 0;
			file_ops.acquire_call_count = 0;
			file_ops.atomic_replace_call_count = 0;
			file_ops.create_new_call_count = 0;
			file_ops.last_atomic_stage.clear();
			file_ops.last_atomic_target.clear();
			file_ops.last_create_stage.clear();
			file_ops.last_create_target.clear();
			*file_ops.lease_destruction_count = 0;
		}

		AshEngine::VegetationOperationControl Control() const
		{
			AshEngine::VegetationOperationControl control{};
			control.cancel_requested = cancellation;
			control.deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(5);
			return control;
		}

		const AshEngine::VegetationChunkSetExpectedIdentity& Expected() const
		{
			return source.Transaction().expected_identity;
		}

		std::filesystem::path ActiveAbsolute() const
		{
			return (std::filesystem::absolute(root.Path()).lexically_normal() /
				source.no_active.active_relative_path).lexically_normal();
		}

		AshEngine::VegetationChunkSetCommitResult Commit()
		{
			return AshEngine::commit_vegetation_chunk_set(
				*prepared, Expected(), Control(), cleanup_registry, file_ops);
		}
	};
}

TEST_CASE("Vegetation baker counter hash and R8 accept limits match v1 vectors")
{
	static_assert(std::is_same_v<
		decltype(AshEngine::VegetationCounterHashResult{}.random),
		std::array<uint64_t, 5>>);

	const AshEngine::VegetationCounterHashResult zero =
		AshEngine::make_vegetation_counter_hash({}, 1);
	CHECK(zero.state == 0x936cd7179cecc6f6ull);
	CHECK(zero.random == std::array<uint64_t, 5>{
		0xb69aaf248fe5723eull,
		0xc9d8c945898ec42bull,
		0x8818d088186f267bull,
		0xfaeb1d600eaa91b7ull,
		0xf432147eb52618d8ull });

	const AshEngine::VegetationCounterHashResult second =
		AshEngine::make_vegetation_counter_hash(SecondCounterKey(), 1);
	CHECK(second.state == 0x1482fb4898b68edaull);
	CHECK(second.random == std::array<uint64_t, 5>{
		0xdbefc5819d9be996ull,
		0xda4acc7ef01435b5ull,
		0xc48fc8b560bbbbe5ull,
		0x3bae788582c73257ull,
		0x6ec202003e5df319ull });

	CHECK(AshEngine::vegetation_candidate_accept_limit(0) == 0u);
	CHECK(AshEngine::vegetation_candidate_accept_limit(1) == 257u);
	CHECK(AshEngine::vegetation_candidate_accept_limit(254) == 65279u);
	CHECK(AshEngine::vegetation_candidate_accept_limit(255) == 65536u);
}

TEST_CASE("Vegetation baker applies R8 boundaries in the real candidate path")
{
	struct R8BakeCase
	{
		const char* name = nullptr;
		uint64_t seed = 0;
		uint8_t threshold = 0;
		uint16_t expected_random_high16 = 0;
		bool expected_present = false;
	};
	const std::array<R8BakeCase, 3> cases{ {
		{ "random equal to limit is rejected", 899, 1, 257, false },
		{ "random one below limit is accepted", 51499, 1, 256, true },
		{ "full threshold accepts uint16 max", 18065, 255, UINT16_MAX, true }
	} };

	for (const R8BakeCase& test_case : cases)
	{
		CAPTURE(test_case.name);
		AshEngine::VegetationBakeInput input = SingleCellGoldenBakeInput(
			test_case.seed, test_case.threshold,
			{ VegetationTest::ReadySurfaceSample(
				0, 1.25, { 0.0, 1.0, 0.0 }) });
		AshEngine::VegetationCounterHashKey key{};
		key.layer_id = input.layer_snapshot->layer_id;
		key.species_id = input.species_snapshots[0]->species_id;
		key.layer_seed = test_case.seed;
		const AshEngine::VegetationCounterHashResult hash =
			AshEngine::make_vegetation_counter_hash(key, 1);
		REQUIRE(static_cast<uint16_t>(hash.random[0] >> 48) ==
			test_case.expected_random_high16);

		const AshEngine::VegetationBakeResult baked =
			AshEngine::bake_vegetation_chunks(
				input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		if (test_case.expected_present)
		{
			REQUIRE(transaction.chunks.size() == 1);
			REQUIRE(transaction.chunks[0].chunk.instances.size() == 1);
			CHECK(transaction.chunks[0].chunk.instances[0].candidate_ordinal == 0);
			CHECK(transaction.removed_coords.empty());
		}
		else
		{
			CHECK(transaction.chunks.empty());
			REQUIRE(transaction.removed_coords.size() == 1);
			CHECK(transaction.removed_coords[0].x == 0);
			CHECK(transaction.removed_coords[0].z == 0);
		}
	}
}

TEST_CASE("Vegetation baker Q12 scale consumes counter stream four")
{
	const AshEngine::VegetationCounterHashResult zero =
		AshEngine::make_vegetation_counter_hash({}, 1);
	const AshEngine::VegetationCounterHashResult collision =
		AshEngine::make_vegetation_counter_hash(SecondCounterKey(36495), 1);
	CHECK(collision.state == 0xa79a3a3f866f9fe0ull);
	CHECK(collision.random[4] == 0xf432d386d1b6c3c9ull);
	CHECK(static_cast<uint16_t>(collision.random[4] >> 48) ==
		static_cast<uint16_t>(zero.random[4] >> 48));

	const AshEngine::VegetationBakeResult collision_baked =
		AshEngine::bake_vegetation_chunks(
			SecondCounterKeyBakeInput(36495),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(collision_baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& collision_transaction =
		RequireTransaction(collision_baked);
	REQUIRE(collision_transaction.chunks.size() == 1);
	const auto collision_ordinal_five = std::find_if(
		collision_transaction.chunks[0].chunk.instances.begin(),
		collision_transaction.chunks[0].chunk.instances.end(),
		[](const AshEngine::VegetationChunkInstance& instance)
		{
			return instance.cell_x == 17 && instance.cell_z == 29 &&
				instance.candidate_ordinal == 5;
		});
	REQUIRE(collision_ordinal_five !=
		collision_transaction.chunks[0].chunk.instances.end());
	CHECK(collision_ordinal_five->scale_q12 == 4839);

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		SecondCounterKeyBakeInput(), VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	REQUIRE(transaction.chunks.size() == 1);
	const auto& instances = transaction.chunks[0].chunk.instances;
	const auto ordinal_five = std::find_if(
		instances.begin(), instances.end(), [](const AshEngine::VegetationChunkInstance& instance)
		{
			return instance.cell_x == 17 && instance.cell_z == 29 &&
				instance.candidate_ordinal == 5;
		});
	REQUIRE(ordinal_five != instances.end());
	CHECK(ordinal_five->scale_q12 == 3986);
}

TEST_CASE("Vegetation baker emits the SDD oct yaw and scale goldens through real bake output")
{
	SUBCASE("axis and world-down normals use exact oct endpoints")
	{
		const AshEngine::VegetationBakeResult baked =
			AshEngine::bake_vegetation_chunks(
				SingleCellGoldenBakeInput(0, 255, {
					VegetationTest::ReadySurfaceSample(
						0, 1.25, { 1.0, 0.0, 0.0 }),
					VegetationTest::ReadySurfaceSample(
						1, 1.25, { 0.0, 0.0, 1.0 }),
					VegetationTest::ReadySurfaceSample(
						2, 1.25, { 0.0, -1.0, 0.0 })
				}),
				VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		REQUIRE(transaction.chunks.size() == 1);
		const auto& instances = transaction.chunks[0].chunk.instances;
		REQUIRE(instances.size() == 3);
		CHECK(instances[0].candidate_ordinal == 0);
		CHECK(instances[0].normal_oct_x == 32767);
		CHECK(instances[0].normal_oct_y == 0);
		CHECK(instances[1].candidate_ordinal == 1);
		CHECK(instances[1].normal_oct_x == 0);
		CHECK(instances[1].normal_oct_y == 32767);
		CHECK(instances[2].candidate_ordinal == 2);
		CHECK(instances[2].normal_oct_x == 32767);
		CHECK(instances[2].normal_oct_y == 32767);
	}

	SUBCASE("yaw consumes stream three high16 0x8000")
	{
		constexpr uint64_t seed = 198969;
		AshEngine::VegetationBakeInput input = SingleCellGoldenBakeInput(
			seed, 255, { VegetationTest::ReadySurfaceSample(
				0, 1.25, { 0.0, 1.0, 0.0 }) });
		AshEngine::VegetationCounterHashKey key{};
		key.layer_id = input.layer_snapshot->layer_id;
		key.species_id = input.species_snapshots[0]->species_id;
		key.layer_seed = seed;
		const AshEngine::VegetationCounterHashResult hash =
			AshEngine::make_vegetation_counter_hash(key, 1);
		REQUIRE(static_cast<uint16_t>(hash.random[3] >> 48) == 0x8000);

		const AshEngine::VegetationBakeResult baked =
			AshEngine::bake_vegetation_chunks(
				input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		REQUIRE(transaction.chunks.size() == 1);
		REQUIRE(transaction.chunks[0].chunk.instances.size() == 1);
		CHECK(transaction.chunks[0].chunk.instances[0].yaw_turn_u16 == 32768);
	}

	SUBCASE("scale consumes stream four midpoint high16 0x8000")
	{
		constexpr uint64_t seed = 241519;
		AshEngine::VegetationBakeInput input = SingleCellGoldenBakeInput(
			seed, 255, { VegetationTest::ReadySurfaceSample(
				0, 1.25, { 0.0, 1.0, 0.0 }) });
		REQUIRE(input.species_snapshots[0]->placement.min_scale_q12 == 3277);
		REQUIRE(input.species_snapshots[0]->placement.max_scale_q12 == 4915);
		AshEngine::VegetationCounterHashKey key{};
		key.layer_id = input.layer_snapshot->layer_id;
		key.species_id = input.species_snapshots[0]->species_id;
		key.layer_seed = seed;
		const AshEngine::VegetationCounterHashResult hash =
			AshEngine::make_vegetation_counter_hash(key, 1);
		REQUIRE(static_cast<uint16_t>(hash.random[4] >> 48) == 0x8000);

		const AshEngine::VegetationBakeResult baked =
			AshEngine::bake_vegetation_chunks(
				input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		REQUIRE(transaction.chunks.size() == 1);
		REQUIRE(transaction.chunks[0].chunk.instances.size() == 1);
		CHECK(transaction.chunks[0].chunk.instances[0].scale_q12 == 4096);
	}
}

TEST_CASE("Vegetation baker ASVI all-absent preimage has exact bytes and digest")
{
	AshEngine::VegetationChunkInputIdentity input{};
	input.cooker_version = 1;
	input.layer_id = VegetationTest::SequentialId(0x00);
	input.layer_seed = 0x0123456789abcdefull;
	input.chunk = { -2, 3 };
	input.surface_identity = VegetationTest::SurfaceIdentity(0x10, 4, 5, 6);

	std::vector<uint8_t> preimage{};
	const AshEngine::VegetationSha256 digest =
		AshEngine::build_vegetation_chunk_input_digest(input, &preimage);
	const std::vector<uint8_t> expected = AllAbsentAsviGoldenBytes();
	REQUIRE(expected.size() == 624);
	CHECK(preimage == expected);
	CHECK(VegetationTest::ToHex(digest) ==
		"8d7e1c07f44858323ffddb12b27daad8ded267169bdf22c7397f366a7cd7d9c3");
}

TEST_CASE("Vegetation baker ASVI present slots and used Species have exact canonical bytes")
{
	AshEngine::VegetationPaletteEntry first =
		VegetationTest::ResolvedMinimalPaletteEntry();
	AshEngine::VegetationPaletteEntry second{};
	second.species_id = VegetationTest::SequentialId(0x80);
	second.species_sha256.fill(0x5a);
	second.species_asset_path = "vegetation/SecondUsed.AshVegetation";
	const std::vector<AshEngine::VegetationPaletteEntry> used_species{ first, second };

	std::array<std::vector<uint8_t>, 64> records{};
	records[0] = { 0x11 };
	records[9] = { 0x22, 0x33, 0x44 };
	records[63] = { 0xfe, 0xff };
	AshEngine::VegetationChunkInputIdentity input{};
	input.cooker_version = 1;
	input.layer_id = VegetationTest::SequentialId(0x20);
	input.layer_seed = 0x1020304050607080ull;
	input.chunk = { -7, 11 };
	input.surface_identity = VegetationTest::SurfaceIdentity(0x40, 12, 13, 14);
	for (size_t slot = 0; slot < records.size(); ++slot)
	{
		input.logical_tiles[slot].present = !records[slot].empty();
		input.logical_tiles[slot].canonical_record = records[slot];
	}
	input.used_species = used_species;

	std::vector<uint8_t> preimage{};
	const AshEngine::VegetationSha256 digest =
		AshEngine::build_vegetation_chunk_input_digest(input, &preimage);
	const std::vector<uint8_t> expected =
		PresentAsviGoldenBytes(records, used_species);
	CHECK(preimage == expected);
	CHECK(digest == AshEngine::vegetation_sha256(expected.data(), expected.size()));

	std::reverse(input.used_species.begin(), input.used_species.end());
	preimage = { 0x77 };
	CHECK(AshEngine::build_vegetation_chunk_input_digest(input, &preimage) ==
		AshEngine::VegetationSha256{});
	CHECK(preimage.empty());
}

TEST_CASE("Vegetation baker seed-only mutation invalidates a deterministic single chunk")
{
	const AshEngine::VegetationBakeResult first = AshEngine::bake_vegetation_chunks(
		SingleChunkBakeInput(0x1234u), VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeResult second = AshEngine::bake_vegetation_chunks(
		SingleChunkBakeInput(0x1235u), VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(first.status == AshEngine::VegetationBakeStatus::Succeeded);
	REQUIRE(second.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& first_transaction =
		RequireTransaction(first);
	const AshEngine::VegetationBakeTransactionOutput& second_transaction =
		RequireTransaction(second);
	REQUIRE(first_transaction.chunks.size() == 1);
	REQUIRE(second_transaction.chunks.size() == 1);
	CHECK(first_transaction.chunks[0].input_digest !=
		second_transaction.chunks[0].input_digest);
	CHECK(first_transaction.chunks[0].object_sha256 !=
		second_transaction.chunks[0].object_sha256);
	CHECK(second_transaction.full_rebake_required);
}

TEST_CASE("Vegetation baker no-active success emits one exact transaction capability")
{
	const AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);

	AshEngine::VegetationChunkSetSourceActiveIdentity source{};
	source.state = AshEngine::VegetationChunkSetSourceActiveState::NoActive;
	CHECK(transaction.source_active_identity == source);
	CHECK(transaction.source_active_identity !=
		AshEngine::VegetationChunkSetSourceActiveIdentity{});
	AshEngine::VegetationChunkSetSourceActiveIdentity changed_source = source;
	changed_source.state = AshEngine::VegetationChunkSetSourceActiveState::Existing;
	CHECK(changed_source != source);
	changed_source = source;
	++changed_source.manifest_sha256[0];
	CHECK(changed_source != source);

	AshEngine::VegetationChunkSetExpectedIdentity expected{};
	expected.operation_serial = input.operation_serial;
	expected.cooker_version = input.cooker_version;
	expected.format_version = 1;
	expected.layer_id = input.layer_snapshot->layer_id;
	expected.layer_generation = input.layer_snapshot->content_generation;
	expected.surface_identity = input.surface_snapshot->identity();
	expected.species_identities.push_back({
		input.layer_snapshot->palette[0].species_id,
		input.layer_snapshot->palette[0].species_sha256 });
	expected.target_coords.push_back({ 0, 0 });
	CHECK(transaction.expected_identity == expected);

	REQUIRE(transaction.chunks.size() == 1);
	REQUIRE(transaction.resulting_manifest.entries.size() == 1);
	CHECK(transaction.resulting_manifest.layer_id == input.layer_snapshot->layer_id);
	CHECK(transaction.resulting_manifest.layer_generation ==
		input.layer_snapshot->content_generation);
	CHECK(transaction.resulting_manifest.surface_identity.surface_id ==
		expected.surface_identity.surface_id);
	CHECK(transaction.resulting_manifest.surface_identity.content_revision ==
		expected.surface_identity.content_revision);
	CHECK(transaction.resulting_manifest.surface_identity.residency_revision ==
		expected.surface_identity.residency_revision);
	CHECK(transaction.resulting_manifest.surface_identity.transform_revision ==
		expected.surface_identity.transform_revision);
	const AshEngine::VegetationChunkSetManifestEntry& entry =
		transaction.resulting_manifest.entries[0];
	CHECK(entry.coord.x == 0);
	CHECK(entry.coord.z == 0);
	CHECK(entry.object_sha256 == transaction.chunks[0].object_sha256);
	CHECK(entry.input_sha256 == transaction.chunks[0].input_digest);

	AshEngine::VegetationChunkSetExpectedIdentity changed = expected;
	++changed.operation_serial;
	CHECK(changed != expected);
	changed = expected;
	++changed.cooker_version;
	CHECK(changed != expected);
	changed = expected;
	++changed.format_version;
	CHECK(changed != expected);
	changed = expected;
	++changed.layer_id[0];
	CHECK(changed != expected);
	changed = expected;
	++changed.layer_generation;
	CHECK(changed != expected);
	changed = expected;
	++changed.surface_identity.surface_id[0];
	CHECK(changed != expected);
	changed = expected;
	++changed.surface_identity.content_revision;
	CHECK(changed != expected);
	changed = expected;
	++changed.surface_identity.residency_revision;
	CHECK(changed != expected);
	changed = expected;
	++changed.surface_identity.transform_revision;
	CHECK(changed != expected);
	changed = expected;
	++changed.species_identities[0].species_id[0];
	CHECK(changed != expected);
	changed = expected;
	++changed.species_identities[0].canonical_sha256[0];
	CHECK(changed != expected);
	changed = expected;
	++changed.target_coords[0].x;
	CHECK(changed != expected);
}

TEST_CASE("Vegetation baker expected species identity covers the complete sorted Layer palette")
{
	SUBCASE("reverse supplied snapshots succeed and expected species stay sorted by ID")
	{
		const AshEngine::VegetationBakeInput input =
			BakeInputWithUnusedSecondSpecies(false);
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		REQUIRE(transaction.expected_identity.species_identities.size() == 2);
		CHECK(transaction.expected_identity.species_identities[0].species_id ==
			input.layer_snapshot->palette[0].species_id);
		CHECK(transaction.expected_identity.species_identities[0].canonical_sha256 ==
			input.layer_snapshot->palette[0].species_sha256);
		CHECK(transaction.expected_identity.species_identities[1].species_id ==
			input.layer_snapshot->palette[1].species_id);
		CHECK(transaction.expected_identity.species_identities[1].canonical_sha256 ==
			input.layer_snapshot->palette[1].species_sha256);
	}

	SUBCASE("missing an unused palette Species fails without a transaction")
	{
		AshEngine::VegetationBakeInput input = BakeInputWithUnusedSecondSpecies(false);
		input.species_snapshots.erase(input.species_snapshots.begin());
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
	}

	SUBCASE("unused Species digest mutation changes only the expected identity")
	{
		const AshEngine::VegetationBakeResult first = AshEngine::bake_vegetation_chunks(
			BakeInputWithUnusedSecondSpecies(false),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeResult second = AshEngine::bake_vegetation_chunks(
			BakeInputWithUnusedSecondSpecies(true),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& first_transaction =
			RequireTransaction(first);
		const AshEngine::VegetationBakeTransactionOutput& second_transaction =
			RequireTransaction(second);
		REQUIRE(first_transaction.chunks.size() == 1);
		REQUIRE(second_transaction.chunks.size() == 1);
		CHECK(first_transaction.chunks[0].input_digest ==
			second_transaction.chunks[0].input_digest);
		CHECK(first_transaction.chunks[0].object_sha256 ==
			second_transaction.chunks[0].object_sha256);
		CHECK(first_transaction.expected_identity != second_transaction.expected_identity);
		AshEngine::VegetationChunkSetExpectedIdentity normalized =
			second_transaction.expected_identity;
		normalized.species_identities[1].canonical_sha256 =
			first_transaction.expected_identity.species_identities[1].canonical_sha256;
		CHECK(normalized == first_transaction.expected_identity);
	}
}

TEST_CASE("Vegetation baker ASVI used Species union publishes a sorted subset with collision-proof total order")
{
	const AshEngine::VegetationBakeInput first_input =
		TwoUsedSpeciesBakeInput(false);
	const AshEngine::VegetationBakeResult first = AshEngine::bake_vegetation_chunks(
		first_input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeResult second = AshEngine::bake_vegetation_chunks(
		TwoUsedSpeciesBakeInput(true),
		VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& first_transaction =
		RequireTransaction(first);
	const AshEngine::VegetationBakeTransactionOutput& second_transaction =
		RequireTransaction(second);
	REQUIRE(first_transaction.chunks.size() == 1);
	REQUIRE(second_transaction.chunks.size() == 1);
	const AshEngine::VegetationBakedChunk& baked = first_transaction.chunks[0];
	REQUIRE(baked.chunk.species.size() == 2);
	REQUIRE(first_input.layer_snapshot->palette.size() == 2);
	for (size_t index = 0; index < baked.chunk.species.size(); ++index)
	{
		CHECK(baked.chunk.species[index].species_id ==
			first_input.layer_snapshot->palette[index].species_id);
		CHECK(baked.chunk.species[index].species_sha256 ==
			first_input.layer_snapshot->palette[index].species_sha256);
		CHECK(baked.chunk.species[index].species_asset_path ==
			first_input.layer_snapshot->palette[index].species_asset_path);
	}
	CHECK(baked.input_digest != second_transaction.chunks[0].input_digest);
	CHECK(baked.object_sha256 != second_transaction.chunks[0].object_sha256);

	REQUIRE(baked.chunk.instances.size() == 16);
	for (size_t index = 0; index < baked.chunk.instances.size(); ++index)
	{
		const AshEngine::VegetationChunkInstance& instance =
			baked.chunk.instances[index];
		CHECK(instance.species_index == index / 8);
		CHECK(instance.candidate_ordinal == index % 8);
		CHECK(instance.normal_oct_x == 0);
		CHECK(instance.normal_oct_y == 0);
		CHECK(instance.world_height_mm == 1250);
		if (index != 0)
		{
			const AshEngine::VegetationChunkInstance& previous =
				baked.chunk.instances[index - 1];
			CHECK(std::tie(previous.species_index, previous.cell_z,
				previous.cell_x, previous.candidate_ordinal) <
				std::tie(instance.species_index, instance.cell_z,
					instance.cell_x, instance.candidate_ordinal));
		}
	}
}

TEST_CASE("Vegetation baker quantizes accepted samples and filters Outside material and slope")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	AshEngine::VegetationSpecies species = *input.species_snapshots[0];
	species.placement.material_slot_min[1] = 1;
	std::vector<uint8_t> canonical_species{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_species(
		species, canonical_species, &error));
	AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
	layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
		canonical_species.data(), canonical_species.size());
	input.layer_snapshot =
		std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
	input.species_snapshots = {
		std::make_shared<const AshEngine::VegetationSpecies>(species) };

	const auto surface = std::const_pointer_cast<
		VegetationTest::ScriptedSurfaceSnapshot>(
			std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
				input.surface_snapshot));
	REQUIRE(surface != nullptr);
	surface->result = {};
	surface->result.status = AshEngine::VegetationSurfaceStatus::Ready;
	surface->result.samples.push_back(VegetationTest::ReadySurfaceSample(
		0, 1.2345, { 1.0, 1.0, 0.0 },
		{ 254, 1, 0, 0, 0, 0, 0, 0 }));
	surface->result.samples.push_back(VegetationTest::NonReadySurfaceSample(
		1, AshEngine::VegetationSurfaceStatus::Outside));
	surface->result.samples.push_back(VegetationTest::ReadySurfaceSample(
		2, 2.0, { 0.0, 1.0, 0.0 },
		{ 255, 0, 0, 0, 0, 0, 0, 0 }));
	surface->result.samples.push_back(VegetationTest::ReadySurfaceSample(
		3, 3.0, { 1.0, 0.0, 0.0 },
		{ 254, 1, 0, 0, 0, 0, 0, 0 }));
	for (uint32_t index = 4; index < 8; ++index)
	{
		surface->result.samples.push_back(VegetationTest::NonReadySurfaceSample(
			index, AshEngine::VegetationSurfaceStatus::Outside));
	}

	const AshEngine::VegetationBakeResult result = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& transaction =
		RequireTransaction(result);
	REQUIRE(transaction.chunks.size() == 1);
	REQUIRE(transaction.chunks[0].chunk.instances.size() == 1);
	const AshEngine::VegetationChunkInstance& instance =
		transaction.chunks[0].chunk.instances[0];
	AshEngine::VegetationCounterHashKey key{};
	key.layer_id = input.layer_snapshot->layer_id;
	key.chunk = { 0, 0 };
	key.species_id = species.species_id;
	key.layer_seed = input.layer_snapshot->layer_seed;
	const AshEngine::VegetationCounterHashResult hash =
		AshEngine::make_vegetation_counter_hash(key, 1);
	CHECK(instance.cell_x == 0);
	CHECK(instance.cell_z == 0);
	CHECK(instance.candidate_ordinal == 0);
	CHECK(instance.cell_fraction_x_u16 ==
		static_cast<uint16_t>(hash.random[1] >> 48));
	CHECK(instance.cell_fraction_z_u16 ==
		static_cast<uint16_t>(hash.random[2] >> 48));
	CHECK(instance.yaw_turn_u16 == static_cast<uint16_t>(hash.random[3] >> 48));
	CHECK(instance.scale_q12 == 3338);
	CHECK(instance.normal_oct_x == 16384);
	CHECK(instance.normal_oct_y == 0);
	CHECK(instance.world_height_mm == 1234);
	CHECK(transaction.chunks[0].chunk.min_world_height_mm == 1234);
	CHECK(transaction.chunks[0].chunk.max_world_height_mm == 1234);
}

TEST_CASE("Vegetation baker source-active CAS shape fails closed")
{
	auto check_rejected = [](const AshEngine::VegetationBakeInput& input)
	{
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
	};

	SUBCASE("default Invalid is not a bake authority")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		input.source_active_identity = {};
		check_rejected(input);
	}

	SUBCASE("NoActive requires a zero digest and null snapshot")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		input.source_active_identity.manifest_sha256.fill(0x21);
		check_rejected(input);
	}

	SUBCASE("Existing requires a nonzero digest and snapshot")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::Existing;
		input.source_active_identity.manifest_sha256.fill(0x21);
		check_rejected(input);
	}

	SUBCASE("snapshot digest must equal the explicit Existing CAS digest")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		input.active_chunk_set = ActiveSnapshotFor(input, 7, { 0, 0 });
		++input.source_active_identity.manifest_sha256[0];
		check_rejected(input);
	}

	SUBCASE("NoActive cannot carry an active snapshot")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
		input.active_chunk_set = ActiveSnapshotFor(input, 7, { 0, 0 });
		input.source_active_identity = {};
		input.source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::NoActive;
		check_rejected(input);
	}
}

TEST_CASE("Vegetation baker permits an empty after-Layer palette transaction")
{
	SUBCASE("NoActive empty authoring produces an empty complete manifest")
	{
		const AshEngine::VegetationBakeInput input = EmptyPaletteBakeInput(7);
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		CHECK(transaction.chunks.empty());
		CHECK(transaction.removed_coords.empty());
		CHECK(transaction.resulting_manifest.entries.empty());
		CHECK(transaction.source_active_identity == input.source_active_identity);
		CHECK(transaction.expected_identity.species_identities.empty());
		CHECK(transaction.expected_identity.target_coords.empty());
	}

	SUBCASE("Existing old coordinate is fully deleted by an empty after-Layer")
	{
		AshEngine::VegetationBakeInput input = EmptyPaletteBakeInput(8);
		const auto surface =
			std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
				input.surface_snapshot);
		REQUIRE(surface != nullptr);
		input.active_chunk_set = ActiveSnapshotFor(input, 7, { 7, -1 });
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		CHECK(transaction.full_rebake_required);
		CHECK(transaction.chunks.empty());
		REQUIRE(transaction.removed_coords.size() == 1);
		CHECK(transaction.removed_coords[0].x == 7);
		CHECK(transaction.removed_coords[0].z == -1);
		CHECK(transaction.resulting_manifest.entries.empty());
		CHECK(transaction.source_active_identity == input.source_active_identity);
		CHECK(transaction.expected_identity.species_identities.empty());
		REQUIRE(transaction.expected_identity.target_coords.size() == 1);
		CHECK(transaction.expected_identity.target_coords[0].x == 7);
		CHECK(transaction.expected_identity.target_coords[0].z == -1);
		CHECK(surface->sample_call_count == 0);
	}
}

TEST_CASE("Vegetation baker matching active identity keeps localized density dirty work")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	input.active_chunk_set = ActiveSnapshotFor(input, 7, { 9, 9 });

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	CHECK_FALSE(transaction.full_rebake_required);
	REQUIRE(transaction.chunks.size() == 1);
	CHECK(transaction.chunks[0].coord.x == 0);
	CHECK(transaction.chunks[0].coord.z == 0);
	CHECK(transaction.removed_coords.empty());
	REQUIRE(transaction.resulting_manifest.entries.size() == 2);
	const AshEngine::VegetationChunkSetManifestEntry& replaced =
		transaction.resulting_manifest.entries[0];
	CHECK(replaced.coord.x == 0);
	CHECK(replaced.coord.z == 0);
	CHECK(replaced.object_sha256 == transaction.chunks[0].object_sha256);
	CHECK(replaced.input_sha256 == transaction.chunks[0].input_digest);
	const AshEngine::VegetationChunkSetManifestEntry& untouched =
		transaction.resulting_manifest.entries[1];
	CHECK(untouched.coord.x == 9);
	CHECK(untouched.coord.z == 9);
	AshEngine::VegetationSha256 expected_object{};
	expected_object.fill(0x41);
	AshEngine::VegetationSha256 expected_input{};
	expected_input.fill(0x51);
	CHECK(untouched.object_sha256 == expected_object);
	CHECK(untouched.input_sha256 == expected_input);
	CHECK(transaction.source_active_identity == input.source_active_identity);
	REQUIRE(transaction.expected_identity.target_coords.size() == 1);
	CHECK(transaction.expected_identity.target_coords[0].x == 0);
	CHECK(transaction.expected_identity.target_coords[0].z == 0);
}

TEST_CASE("Vegetation baker zero-instance dirty chunk deletes only that manifest entry")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	AshEngine::VegetationBakeInput input = RejectHeavyBakeInput(surface);
	input.active_chunk_set = ActiveSnapshotForCoords(input, 7, { { 9, 9 }, { 0, 0 } });

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	CHECK(transaction.chunks.empty());
	REQUIRE(transaction.removed_coords.size() == 1);
	CHECK(transaction.removed_coords[0].x == 0);
	CHECK(transaction.removed_coords[0].z == 0);
	REQUIRE(transaction.resulting_manifest.entries.size() == 1);
	const AshEngine::VegetationChunkSetManifestEntry& untouched =
		transaction.resulting_manifest.entries[0];
	CHECK(untouched.coord.x == 9);
	CHECK(untouched.coord.z == 9);
	AshEngine::VegetationSha256 expected_object{};
	expected_object.fill(0x42);
	AshEngine::VegetationSha256 expected_input{};
	expected_input.fill(0x52);
	CHECK(untouched.object_sha256 == expected_object);
	CHECK(untouched.input_sha256 == expected_input);
	CHECK(transaction.source_active_identity == input.source_active_identity);
}

TEST_CASE("Vegetation baker empty Existing source keeps CAS and canonical z-x targets")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	input.active_chunk_set = ActiveSnapshotForCoords(input, 7, {});
	input.dirty_evidence.density_coords = {
		{ -7, 2 }, { 7, -1 }, { -7, 2 }, { 7, -1 } };

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	CHECK_FALSE(transaction.full_rebake_required);
	CHECK(transaction.source_active_identity == input.source_active_identity);
	CHECK(transaction.source_active_identity.state ==
		AshEngine::VegetationChunkSetSourceActiveState::Existing);
	REQUIRE(transaction.expected_identity.target_coords.size() == 2);
	CHECK(transaction.expected_identity.target_coords[0].x == 7);
	CHECK(transaction.expected_identity.target_coords[0].z == -1);
	CHECK(transaction.expected_identity.target_coords[1].x == -7);
	CHECK(transaction.expected_identity.target_coords[1].z == 2);
	REQUIRE(transaction.removed_coords.size() == 2);
	CHECK(transaction.removed_coords[0].x == 7);
	CHECK(transaction.removed_coords[0].z == -1);
	CHECK(transaction.removed_coords[1].x == -7);
	CHECK(transaction.removed_coords[1].z == 2);
	CHECK(transaction.resulting_manifest.entries.empty());
}

TEST_CASE("Vegetation baker unsafe generation mismatch expands manifest and authoring union")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1235u, 8);
	input.dirty_evidence = {};
	input.dirty_evidence.generation = 8;
	input.active_chunk_set = ActiveSnapshotFor(input, 7, { 9, 9 });

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	CHECK(transaction.full_rebake_required);
	REQUIRE(transaction.chunks.size() == 1);
	CHECK(transaction.chunks[0].coord.x == 0);
	CHECK(transaction.chunks[0].coord.z == 0);
	REQUIRE(transaction.removed_coords.size() == 1);
	CHECK(transaction.removed_coords[0].x == 9);
	CHECK(transaction.removed_coords[0].z == 9);
	REQUIRE(transaction.resulting_manifest.entries.size() == 1);
	CHECK(transaction.resulting_manifest.layer_id == input.layer_snapshot->layer_id);
	CHECK(transaction.resulting_manifest.layer_generation == 8);
	CHECK(transaction.resulting_manifest.entries[0].coord.x == 0);
	CHECK(transaction.resulting_manifest.entries[0].coord.z == 0);
	CHECK(transaction.resulting_manifest.entries[0].object_sha256 ==
		transaction.chunks[0].object_sha256);
	CHECK(transaction.source_active_identity == input.source_active_identity);
	REQUIRE(transaction.expected_identity.target_coords.size() == 2);
	CHECK(transaction.expected_identity.target_coords[0].x == 0);
	CHECK(transaction.expected_identity.target_coords[0].z == 0);
	CHECK(transaction.expected_identity.target_coords[1].x == 9);
	CHECK(transaction.expected_identity.target_coords[1].z == 9);
}

TEST_CASE("Vegetation baker seed changes cause real absent and present output transitions")
{
	auto make_seed_input = [](const uint64_t seed, const uint64_t generation)
	{
		return SingleCellGoldenBakeInput(
			seed, 1, { VegetationTest::ReadySurfaceSample(
				0, 1.25, { 0.0, 1.0, 0.0 }) }, generation);
	};
	struct SeedTransition
	{
		const char* name = nullptr;
		uint64_t before_seed = 0;
		uint64_t after_seed = 0;
		bool before_present = false;
		bool after_present = false;
	};
	const std::array<SeedTransition, 2> transitions{ {
		{ "absent to present", 899, 51499, false, true },
		{ "present to absent", 51499, 899, true, false }
	} };

	for (const SeedTransition& transition : transitions)
	{
		CAPTURE(transition.name);
		AshEngine::VegetationBakeInput before_input =
			make_seed_input(transition.before_seed, 7);
		AshEngine::VegetationBakeInput after_input =
			make_seed_input(transition.after_seed, 8);
		CHECK(CanonicalLayerWithNormalizedGenerationAndSeed(
			*before_input.layer_snapshot, 7, 0) ==
			CanonicalLayerWithNormalizedGenerationAndSeed(
				*after_input.layer_snapshot, 7, 0));
		CHECK(before_input.surface_snapshot->identity().surface_id ==
			after_input.surface_snapshot->identity().surface_id);
		CHECK(before_input.surface_snapshot->identity().content_revision ==
			after_input.surface_snapshot->identity().content_revision);

		const AshEngine::VegetationBakeResult before_baked =
			AshEngine::bake_vegetation_chunks(
				before_input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& before_transaction =
			RequireTransaction(before_baked);
		CHECK((before_transaction.chunks.size() == 1) ==
			transition.before_present);
		CHECK((before_transaction.removed_coords.size() == 1) ==
			!transition.before_present);

		after_input.dirty_evidence = {};
		after_input.dirty_evidence.generation = 8;
		std::vector<AshEngine::VegetationChunkCoord> active_coords{ { 9, 9 } };
		if (transition.before_present)
		{
			active_coords.push_back({ 0, 0 });
		}
		auto active = std::const_pointer_cast<
			AshEngine::VegetationActiveChunkSetSnapshot>(
				ActiveSnapshotForCoords(after_input, 7, std::move(active_coords)));
		if (transition.before_present)
		{
			REQUIRE(before_transaction.chunks.size() == 1);
			active->entries[0].object_sha256 =
				before_transaction.chunks[0].object_sha256;
			active->entries[0].input_sha256 =
				before_transaction.chunks[0].input_digest;
		}
		const size_t active_entry_count = active->entries.size();
		after_input.active_chunk_set = active;

		const AshEngine::VegetationBakeResult after_baked =
			AshEngine::bake_vegetation_chunks(
				after_input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(after_baked);
		CHECK(transaction.full_rebake_required);
		REQUIRE(transaction.expected_identity.target_coords.size() == 2);
		CHECK(transaction.expected_identity.target_coords[0].x == 0);
		CHECK(transaction.expected_identity.target_coords[0].z == 0);
		CHECK(transaction.expected_identity.target_coords[1].x == 9);
		CHECK(transaction.expected_identity.target_coords[1].z == 9);
		CHECK((transaction.chunks.size() == 1) == transition.after_present);
		if (transition.after_present)
		{
			REQUIRE(transaction.chunks.size() == 1);
			CHECK(transaction.chunks[0].coord.x == 0);
			CHECK(transaction.chunks[0].coord.z == 0);
			REQUIRE(transaction.removed_coords.size() == 1);
			CHECK(transaction.removed_coords[0].x == 9);
			CHECK(transaction.removed_coords[0].z == 9);
			REQUIRE(transaction.resulting_manifest.entries.size() == 1);
			CHECK(transaction.resulting_manifest.entries[0].coord.x == 0);
			CHECK(transaction.resulting_manifest.entries[0].coord.z == 0);
		}
		else
		{
			CHECK(transaction.chunks.empty());
			REQUIRE(transaction.removed_coords.size() == 2);
			CHECK(transaction.removed_coords[0].x == 0);
			CHECK(transaction.removed_coords[0].z == 0);
			CHECK(transaction.removed_coords[1].x == 9);
			CHECK(transaction.removed_coords[1].z == 9);
			CHECK(transaction.resulting_manifest.entries.empty());
		}
		CHECK(transaction.source_active_identity ==
			after_input.source_active_identity);
		CHECK(active->entries.size() == active_entry_count);
	}

	const AshEngine::VegetationBakeResult first_present =
		AshEngine::bake_vegetation_chunks(
			make_seed_input(51499, 7),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeResult second_present =
		AshEngine::bake_vegetation_chunks(
			make_seed_input(113, 7),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& first_present_transaction =
		RequireTransaction(first_present);
	const AshEngine::VegetationBakeTransactionOutput& second_present_transaction =
		RequireTransaction(second_present);
	REQUIRE(first_present_transaction.chunks.size() == 1);
	REQUIRE(second_present_transaction.chunks.size() == 1);
	CHECK(first_present_transaction.chunks[0].input_digest !=
		second_present_transaction.chunks[0].input_digest);
	CHECK(first_present_transaction.chunks[0].object_sha256 !=
		second_present_transaction.chunks[0].object_sha256);
}

TEST_CASE("Vegetation baker surface revision changes cause real absent and present output transitions")
{
	auto make_surface_input = [](const uint64_t content_revision, const bool ready)
	{
		std::vector<AshEngine::VegetationSurfaceSample> samples{};
		if (ready)
		{
			samples.push_back(VegetationTest::ReadySurfaceSample(
				0, 1.25, { 0.0, 1.0, 0.0 }));
		}
		else
		{
			samples.push_back(VegetationTest::NonReadySurfaceSample(
				0, AshEngine::VegetationSurfaceStatus::Outside));
		}
		AshEngine::VegetationBakeInput input =
			SingleCellGoldenBakeInput(51499, 255, std::move(samples), 7);
		auto surface = std::const_pointer_cast<
			VegetationTest::ScriptedSurfaceSnapshot>(
				std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
					input.surface_snapshot));
		if (!surface)
		{
			throw std::runtime_error(
				"Surface transition fixture could not recover its surface");
		}
		surface->identity_before =
			VegetationTest::SurfaceIdentity(0x10, content_revision, 5, 6);
		surface->identity_after = surface->identity_before;
		return input;
	};
	struct SurfaceTransition
	{
		const char* name = nullptr;
		bool before_ready = false;
		bool after_ready = false;
	};
	const std::array<SurfaceTransition, 2> transitions{ {
		{ "revision-backed absent to present", false, true },
		{ "revision-backed present to absent", true, false }
	} };

	for (const SurfaceTransition& transition : transitions)
	{
		CAPTURE(transition.name);
		AshEngine::VegetationBakeInput before_input =
			make_surface_input(4, transition.before_ready);
		AshEngine::VegetationBakeInput after_input =
			make_surface_input(5, transition.after_ready);
		CHECK(CanonicalLayerWithNormalizedGenerationAndSeed(
			*before_input.layer_snapshot, 7, 51499) ==
			CanonicalLayerWithNormalizedGenerationAndSeed(
				*after_input.layer_snapshot, 7, 51499));
		const AshEngine::VegetationSurfaceIdentity before_identity =
			before_input.surface_snapshot->identity();
		const AshEngine::VegetationSurfaceIdentity after_identity =
			after_input.surface_snapshot->identity();
		CHECK(before_identity.surface_id == after_identity.surface_id);
		CHECK(before_identity.content_revision == 4);
		CHECK(after_identity.content_revision == 5);
		CHECK(before_identity.residency_revision ==
			after_identity.residency_revision);
		CHECK(before_identity.transform_revision ==
			after_identity.transform_revision);

		const AshEngine::VegetationBakeResult before_baked =
			AshEngine::bake_vegetation_chunks(
				before_input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& before_transaction =
			RequireTransaction(before_baked);
		CHECK((before_transaction.chunks.size() == 1) ==
			transition.before_ready);

		after_input.dirty_evidence = {};
		after_input.dirty_evidence.generation = 7;
		std::vector<AshEngine::VegetationChunkCoord> active_coords{ { 9, 9 } };
		if (transition.before_ready)
		{
			active_coords.push_back({ 0, 0 });
		}
		auto active = std::const_pointer_cast<
			AshEngine::VegetationActiveChunkSetSnapshot>(
				ActiveSnapshotForCoords(after_input, 7, std::move(active_coords)));
		active->surface_identity = before_identity;
		if (transition.before_ready)
		{
			REQUIRE(before_transaction.chunks.size() == 1);
			active->entries[0].object_sha256 =
				before_transaction.chunks[0].object_sha256;
			active->entries[0].input_sha256 =
				before_transaction.chunks[0].input_digest;
		}
		const size_t active_entry_count = active->entries.size();
		after_input.active_chunk_set = active;

		const AshEngine::VegetationBakeResult after_baked =
			AshEngine::bake_vegetation_chunks(
				after_input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(after_baked);
		CHECK(transaction.full_rebake_required);
		REQUIRE(transaction.expected_identity.target_coords.size() == 2);
		CHECK(transaction.expected_identity.target_coords[0].x == 0);
		CHECK(transaction.expected_identity.target_coords[0].z == 0);
		CHECK(transaction.expected_identity.target_coords[1].x == 9);
		CHECK(transaction.expected_identity.target_coords[1].z == 9);
		CHECK((transaction.chunks.size() == 1) == transition.after_ready);
		if (transition.after_ready)
		{
			REQUIRE(transaction.removed_coords.size() == 1);
			CHECK(transaction.removed_coords[0].x == 9);
			CHECK(transaction.removed_coords[0].z == 9);
		}
		else
		{
			REQUIRE(transaction.removed_coords.size() == 2);
			CHECK(transaction.removed_coords[0].x == 0);
			CHECK(transaction.removed_coords[0].z == 0);
			CHECK(transaction.removed_coords[1].x == 9);
			CHECK(transaction.removed_coords[1].z == 9);
		}
		CHECK(transaction.source_active_identity ==
			after_input.source_active_identity);
		CHECK(active->entries.size() == active_entry_count);
	}

	const AshEngine::VegetationBakeResult first_present =
		AshEngine::bake_vegetation_chunks(
			make_surface_input(4, true),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeResult second_present =
		AshEngine::bake_vegetation_chunks(
			make_surface_input(5, true),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& first_present_transaction =
		RequireTransaction(first_present);
	const AshEngine::VegetationBakeTransactionOutput& second_present_transaction =
		RequireTransaction(second_present);
	REQUIRE(first_present_transaction.chunks.size() == 1);
	REQUIRE(second_present_transaction.chunks.size() == 1);
	CHECK(first_present_transaction.chunks[0].input_digest !=
		second_present_transaction.chunks[0].input_digest);
	CHECK(first_present_transaction.chunks[0].object_sha256 !=
		second_present_transaction.chunks[0].object_sha256);
}

TEST_CASE("Vegetation baker full dirty mapping floors negative tile axes by eight")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 8);
	input.surface_snapshot = surface;
	AshEngine::VegetationLayerSnapshot layer = *input.layer_snapshot;
	const AshEngine::VegetationLayerTile prototype = layer.tiles[0];
	layer.tiles.clear();
	for (const AshEngine::VegetationChunkCoord tile_coord :
		std::array<AshEngine::VegetationChunkCoord, 6>{ {
			{ -1, 0 }, { -8, 0 }, { -9, 0 },
			{ 0, -1 }, { 0, -8 }, { 0, -9 }
		} })
	{
		AshEngine::VegetationLayerTile tile = prototype;
		tile.tile_x = tile_coord.x;
		tile.tile_z = tile_coord.z;
		layer.tiles.push_back(std::move(tile));
	}
	std::sort(layer.tiles.begin(), layer.tiles.end(),
		[](const AshEngine::VegetationLayerTile& lhs,
			const AshEngine::VegetationLayerTile& rhs)
		{
			return lhs.tile_z != rhs.tile_z
				? lhs.tile_z < rhs.tile_z
				: lhs.tile_x < rhs.tile_x;
		});
	input.layer_snapshot =
		std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(layer));
	input.dirty_evidence = {};
	input.dirty_evidence.generation = 8;
	input.active_chunk_set = ActiveSnapshotFor(input, 7, { 9, 9 });

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction =
		RequireTransaction(baked);
	CHECK(transaction.full_rebake_required);
	const std::vector<AshEngine::VegetationChunkCoord> expected{
		{ 0, -2 }, { 0, -1 }, { -2, 0 }, { -1, 0 }, { 9, 9 }
	};
	REQUIRE(transaction.expected_identity.target_coords.size() == expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
	{
		CHECK(transaction.expected_identity.target_coords[index].x == expected[index].x);
		CHECK(transaction.expected_identity.target_coords[index].z == expected[index].z);
	}
	REQUIRE(transaction.chunks.size() == 4);
	CHECK(transaction.chunks[0].coord.x == 0);
	CHECK(transaction.chunks[0].coord.z == -2);
	CHECK(transaction.chunks[1].coord.x == 0);
	CHECK(transaction.chunks[1].coord.z == -1);
	CHECK(transaction.chunks[2].coord.x == -2);
	CHECK(transaction.chunks[2].coord.z == 0);
	CHECK(transaction.chunks[3].coord.x == -1);
	CHECK(transaction.chunks[3].coord.z == 0);
	REQUIRE(transaction.removed_coords.size() == 1);
	CHECK(transaction.removed_coords[0].x == 9);
	CHECK(transaction.removed_coords[0].z == 9);
}

TEST_CASE("Vegetation baker localizes only evidence based on the active generation")
{
	SUBCASE("working set opened after active generation forces full union")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1235u, 11);
		input.dirty_evidence.base_generation = 10;
		input.active_chunk_set = ActiveSnapshotFor(input, 7, { 9, 9 });
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		CHECK(transaction.full_rebake_required);
		REQUIRE(transaction.removed_coords.size() == 1);
		CHECK(transaction.removed_coords[0].x == 9);
		CHECK(transaction.removed_coords[0].z == 9);
	}

	SUBCASE("evidence based on active generation stays localized")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1235u, 11);
		input.dirty_evidence.base_generation = 10;
		input.active_chunk_set = ActiveSnapshotFor(input, 10, { 9, 9 });
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		CHECK_FALSE(transaction.full_rebake_required);
		CHECK(transaction.removed_coords.empty());
	}
}

TEST_CASE("Vegetation baker species dirty union has exact exclusive manifest before and after arms")
{
	const AshEngine::VegetationChunkCoord manifest_only{ 5, 2 };
	const AshEngine::VegetationChunkCoord patch_before_only{ -2, 1 };
	const AshEngine::VegetationChunkCoord after_snapshot_only{ 3, -1 };
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	const AshEngine::VegetationId removed_species =
		input.layer_snapshot->palette[0].species_id;
	AshEngine::VegetationSpecies added_species = *input.species_snapshots[0];
	added_species.species_id = VegetationTest::SequentialId(0x80);
	added_species.name = "After-only Species";
	std::vector<uint8_t> canonical_added_species{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_species(
		added_species, canonical_added_species, &error));
	AshEngine::VegetationPaletteEntry added_palette{};
	added_palette.species_id = added_species.species_id;
	added_palette.species_sha256 = AshEngine::vegetation_sha256(
		canonical_added_species.data(), canonical_added_species.size());
	added_palette.species_asset_path =
		"vegetation/AfterOnly.AshVegetation";

	AshEngine::VegetationLayerSnapshot before = *input.layer_snapshot;
	before.tiles[0].tile_x = patch_before_only.x * 8;
	before.tiles[0].tile_z = patch_before_only.z * 8;
	input.layer_snapshot =
		std::make_shared<const AshEngine::VegetationLayerSnapshot>(std::move(before));
	const uint64_t before_generation = input.layer_snapshot->content_generation;
	AshEngine::VegetationLayerWorkingSet working(input.layer_snapshot);

	AshEngine::VegetationPaletteEdit remove{};
	remove.mode = AshEngine::VegetationPaletteEditMode::Remove;
	remove.target_species_id = removed_species;
	remove.clear_weights = true;
	REQUIRE(AshEngine::apply_vegetation_palette_edit(working, remove).status ==
		AshEngine::VegetationMutationStatus::Applied);
	AshEngine::VegetationPaletteEdit add{};
	add.mode = AshEngine::VegetationPaletteEditMode::Add;
	add.replacement = added_palette;
	REQUIRE(AshEngine::apply_vegetation_palette_edit(working, add).status ==
		AshEngine::VegetationMutationStatus::Applied);

	AshEngine::VegetationBrushStroke paint{};
	paint.mode = AshEngine::VegetationBrushMode::Paint;
	paint.selected_species = added_species.species_id;
	paint.radius_mm = 250;
	paint.strength = 255;
	paint.falloff = 0;
	paint.spacing_mm = 1;
	paint.stroke_seed = 0x1020304050607080ull;
	AshEngine::VegetationSurfaceSampleRequest paint_point{};
	paint_point.chunk = after_snapshot_only;
	paint_point.local_xz = { 0.5, 0.5 };
	paint.path.push_back(paint_point);
	REQUIRE(AshEngine::apply_vegetation_brush_stroke(working, paint).status ==
		AshEngine::VegetationMutationStatus::Applied);

	input.layer_snapshot = working.publish_snapshot();
	input.dirty_evidence = working.snapshot_bake_dirty_evidence();
	input.dirty_evidence.density_coords.clear();
	input.species_snapshots = {
		std::make_shared<const AshEngine::VegetationSpecies>(
			std::move(added_species)) };
	REQUIRE(input.dirty_evidence.base_generation == before_generation);
	REQUIRE(input.dirty_evidence.generation ==
		input.layer_snapshot->content_generation);
	REQUIRE(input.dirty_evidence.species_coords.size() == 2);
	const auto& removed = input.dirty_evidence.species_coords[0];
	const auto& added = input.dirty_evidence.species_coords[1];
	CHECK(removed.species_id == removed_species);
	REQUIRE(removed.before_coords.size() == 1);
	CHECK(removed.before_coords[0].x == patch_before_only.x);
	CHECK(removed.before_coords[0].z == patch_before_only.z);
	CHECK(removed.after_coords.empty());
	CHECK(added.species_id == added_palette.species_id);
	CHECK(added.before_coords.empty());
	REQUIRE(added.after_coords.size() == 1);
	CHECK(added.after_coords[0].x == after_snapshot_only.x);
	CHECK(added.after_coords[0].z == after_snapshot_only.z);

	auto active = std::const_pointer_cast<
		AshEngine::VegetationActiveChunkSetSnapshot>(
			ActiveSnapshotForCoords(input, before_generation, { manifest_only }));
	REQUIRE(active->entries.size() == 1);
	active->entries[0].referenced_species_ids = { removed_species };
	input.active_chunk_set = active;

	const AshEngine::VegetationBakeResult result =
		AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& transaction =
		RequireTransaction(result);
	CHECK_FALSE(transaction.full_rebake_required);
	const std::array<AshEngine::VegetationChunkCoord, 3> expected{ {
		after_snapshot_only, patch_before_only, manifest_only
	} };
	REQUIRE(transaction.expected_identity.target_coords.size() == expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
	{
		CHECK(transaction.expected_identity.target_coords[index].x == expected[index].x);
		CHECK(transaction.expected_identity.target_coords[index].z == expected[index].z);
	}
	REQUIRE(transaction.chunks.size() == 1);
	CHECK(transaction.chunks[0].coord.x == after_snapshot_only.x);
	CHECK(transaction.chunks[0].coord.z == after_snapshot_only.z);
	REQUIRE(transaction.removed_coords.size() == 2);
	CHECK(transaction.removed_coords[0].x == patch_before_only.x);
	CHECK(transaction.removed_coords[0].z == patch_before_only.z);
	CHECK(transaction.removed_coords[1].x == manifest_only.x);
	CHECK(transaction.removed_coords[1].z == manifest_only.z);
	REQUIRE(transaction.resulting_manifest.entries.size() == 1);
	CHECK(transaction.resulting_manifest.entries[0].coord.x ==
		after_snapshot_only.x);
	CHECK(transaction.resulting_manifest.entries[0].coord.z ==
		after_snapshot_only.z);
}

TEST_CASE("Vegetation baker external Species digest change uses current coords for before and after")
{
	const AshEngine::VegetationChunkCoord current_authoring{ 0, 0 };
	const AshEngine::VegetationChunkCoord manifest_reference{ 5, 5 };
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	AshEngine::VegetationSpecies current_species = *input.species_snapshots[0];
	current_species.name = "Externally changed Species";
	std::vector<uint8_t> canonical_species{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_species(
		current_species, canonical_species, &error));
	AshEngine::VegetationLayerSnapshot current_layer = *input.layer_snapshot;
	const AshEngine::VegetationSha256 old_digest =
		current_layer.palette[0].species_sha256;
	current_layer.palette[0].species_sha256 = AshEngine::vegetation_sha256(
		canonical_species.data(), canonical_species.size());
	REQUIRE(current_layer.palette[0].species_sha256 != old_digest);
	input.layer_snapshot =
		std::make_shared<const AshEngine::VegetationLayerSnapshot>(
			std::move(current_layer));
	input.species_snapshots = {
		std::make_shared<const AshEngine::VegetationSpecies>(
			std::move(current_species)) };
	input.dirty_evidence = {};
	input.dirty_evidence.base_generation =
		input.layer_snapshot->content_generation;
	input.dirty_evidence.generation =
		input.layer_snapshot->content_generation;
	AshEngine::VegetationAuthoringSpeciesDirtyEvidence external{};
	external.species_id = input.layer_snapshot->palette[0].species_id;
	external.before_coords = { current_authoring };
	external.after_coords = { current_authoring };
	input.dirty_evidence.species_coords.push_back(external);

	auto active = std::const_pointer_cast<
		AshEngine::VegetationActiveChunkSetSnapshot>(
			ActiveSnapshotForCoords(input,
				input.layer_snapshot->content_generation,
				{ manifest_reference }));
	active->entries[0].referenced_species_ids = { external.species_id };
	input.active_chunk_set = active;

	const AshEngine::VegetationBakeResult result =
		AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& transaction =
		RequireTransaction(result);
	CHECK_FALSE(transaction.full_rebake_required);
	REQUIRE(transaction.expected_identity.target_coords.size() == 2);
	CHECK(transaction.expected_identity.target_coords[0].x ==
		current_authoring.x);
	CHECK(transaction.expected_identity.target_coords[0].z ==
		current_authoring.z);
	CHECK(transaction.expected_identity.target_coords[1].x ==
		manifest_reference.x);
	CHECK(transaction.expected_identity.target_coords[1].z ==
		manifest_reference.z);
	REQUIRE(transaction.chunks.size() == 1);
	CHECK(transaction.chunks[0].coord.x == current_authoring.x);
	CHECK(transaction.chunks[0].coord.z == current_authoring.z);
	REQUIRE(transaction.removed_coords.size() == 1);
	CHECK(transaction.removed_coords[0].x == manifest_reference.x);
	CHECK(transaction.removed_coords[0].z == manifest_reference.z);
}

TEST_CASE("Vegetation baker rejects a surface that changed after initial capture")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u);
	const auto surface = std::const_pointer_cast<VegetationTest::ScriptedSurfaceSnapshot>(
		std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
			input.surface_snapshot));
	REQUIRE(surface != nullptr);
	surface->identity_after = VegetationTest::SurfaceIdentity(0x20, 40, 50, 60);

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
	CHECK_FALSE(baked.transaction.has_value());
}

TEST_CASE("Vegetation baker streams accepted candidates through bounded surface batches")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		DenseTwoBatchBakeInput(surface),
		VegetationTest::ActiveControl(std::chrono::seconds(5)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	REQUIRE(transaction.chunks.size() == 1);
	CHECK(transaction.chunks[0].chunk.instances.size() == 4352);
	CHECK(surface->batch_count == 2);
	CHECK(surface->max_batch_size <= 4096);
}

TEST_CASE("Vegetation baker distant sparse tiles do not change local objects or surface work")
{
	auto baseline_surface = std::make_shared<BatchedSurfaceSnapshot>();
	AshEngine::VegetationBakeInput baseline_input =
		TwoDirtyChunkBakeInput(baseline_surface);
	baseline_input.active_chunk_set =
		ActiveSnapshotForCoords(baseline_input, 7, {});
	const AshEngine::VegetationBakeResult baseline =
		AshEngine::bake_vegetation_chunks(
			baseline_input,
			VegetationTest::ActiveControl(std::chrono::seconds(5)));
	const AshEngine::VegetationBakeTransactionOutput& baseline_transaction =
		RequireTransaction(baseline);
	REQUIRE(baseline_transaction.chunks.size() == 2);
	REQUIRE(baseline_surface->batch_count == 2);

	auto sparse_surface = std::make_shared<BatchedSurfaceSnapshot>();
	AshEngine::VegetationBakeInput sparse_input =
		TwoDirtyChunkBakeInput(sparse_surface);
	AshEngine::VegetationLayerSnapshot sparse_layer = *sparse_input.layer_snapshot;
	const AshEngine::VegetationLayerTile prototype = sparse_layer.tiles[0];
	for (int64_t index = 0; index < 256; ++index)
	{
		AshEngine::VegetationLayerTile distant = prototype;
		distant.tile_x = 1000 + index;
		distant.tile_z = 1000;
		sparse_layer.tiles.push_back(std::move(distant));
	}
	std::sort(sparse_layer.tiles.begin(), sparse_layer.tiles.end(),
		[](const AshEngine::VegetationLayerTile& lhs,
			const AshEngine::VegetationLayerTile& rhs)
		{
			return lhs.tile_z != rhs.tile_z
				? lhs.tile_z < rhs.tile_z
				: lhs.tile_x < rhs.tile_x;
		});
	sparse_input.layer_snapshot =
		std::make_shared<const AshEngine::VegetationLayerSnapshot>(
			std::move(sparse_layer));
	sparse_input.active_chunk_set =
		ActiveSnapshotForCoords(sparse_input, 7, {});

	const AshEngine::VegetationBakeResult sparse =
		AshEngine::bake_vegetation_chunks(
			sparse_input, VegetationTest::ActiveControl(std::chrono::seconds(5)));
	const AshEngine::VegetationBakeTransactionOutput& sparse_transaction =
		RequireTransaction(sparse);
	REQUIRE(sparse_transaction.chunks.size() ==
		baseline_transaction.chunks.size());
	CHECK(sparse_surface->batch_count == baseline_surface->batch_count);
	CHECK(sparse_surface->max_batch_size == baseline_surface->max_batch_size);
	for (size_t index = 0; index < baseline_transaction.chunks.size(); ++index)
	{
		CHECK(sparse_transaction.chunks[index].coord.x ==
			baseline_transaction.chunks[index].coord.x);
		CHECK(sparse_transaction.chunks[index].coord.z ==
			baseline_transaction.chunks[index].coord.z);
		CHECK(sparse_transaction.chunks[index].input_digest ==
			baseline_transaction.chunks[index].input_digest);
		CHECK(sparse_transaction.chunks[index].object_sha256 ==
			baseline_transaction.chunks[index].object_sha256);
		CHECK(sparse_transaction.chunks[index].object_bytes ==
			baseline_transaction.chunks[index].object_bytes);
	}
}

TEST_CASE("Vegetation baker completes a reject-heavy cell without sampling")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		RejectHeavyBakeInput(surface),
		VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	const AshEngine::VegetationBakeTransactionOutput& transaction = RequireTransaction(baked);
	CHECK(transaction.chunks.empty());
	REQUIRE(transaction.removed_coords.size() == 1);
	CHECK(transaction.removed_coords[0].x == 0);
	CHECK(transaction.removed_coords[0].z == 0);
	CHECK(surface->batch_count == 0);
}

TEST_CASE("Vegetation baker detects surface identity changes at the inner attempt checkpoint")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	surface->change_identity_on_call = 2;
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		InnerCheckpointBakeInput(surface),
		VegetationTest::ActiveControl(std::chrono::seconds(2)));
	CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
	CHECK_FALSE(baked.transaction.has_value());
	CHECK(baked.error.find("during candidate generation") != std::string::npos);
	CHECK(surface->batch_count == 0);
}

TEST_CASE("Vegetation baker second surface batch failure preserves detail and no partial output")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	surface->second_batch_behavior = BatchedSurfaceSnapshot::SecondBatchBehavior::Fail;
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		DenseTwoBatchBakeInput(surface),
		VegetationTest::ActiveControl(std::chrono::seconds(5)));
	CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
	CHECK_FALSE(baked.transaction.has_value());
	CHECK(baked.error.find("Vegetation surface sampling failed.") != std::string::npos);
	CHECK(surface->batch_count == 2);
	CHECK(surface->max_batch_size <= 4096);
}

TEST_CASE("Vegetation baker second surface batch cancellation returns no partial output")
{
	auto surface = std::make_shared<BatchedSurfaceSnapshot>();
	auto cancellation = std::make_shared<std::atomic_bool>(false);
	surface->second_batch_behavior = BatchedSurfaceSnapshot::SecondBatchBehavior::Cancel;
	surface->cancellation = cancellation;
	AshEngine::VegetationOperationControl control{};
	control.cancel_requested = cancellation;
	control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		DenseTwoBatchBakeInput(surface), control);
	CHECK(baked.status == AshEngine::VegetationBakeStatus::Cancelled);
	CHECK_FALSE(baked.transaction.has_value());
	CHECK(surface->batch_count == 2);
	CHECK(surface->max_batch_size <= 4096);
}

TEST_CASE("Vegetation baker later dirty coordinate failure discards the completed prefix")
{
	auto check_batch_order = [](const BatchedSurfaceSnapshot& surface)
	{
		REQUIRE(surface.batch_count == 2);
		REQUIRE(surface.batch_chunks.size() == 2);
		CHECK(surface.batch_chunks[0].x == 0);
		CHECK(surface.batch_chunks[0].z == 0);
		CHECK(surface.batch_chunks[1].x == 1);
		CHECK(surface.batch_chunks[1].z == 0);
	};

	SUBCASE("second dirty chunk fails after the first completed batch")
	{
		auto surface = std::make_shared<BatchedSurfaceSnapshot>();
		surface->second_batch_behavior = BatchedSurfaceSnapshot::SecondBatchBehavior::Fail;
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			TwoDirtyChunkBakeInput(surface),
			VegetationTest::ActiveControl(std::chrono::seconds(5)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
		check_batch_order(*surface);
	}

	SUBCASE("second dirty chunk Pending discards the completed first chunk")
	{
		auto surface = std::make_shared<BatchedSurfaceSnapshot>();
		surface->second_batch_behavior =
			BatchedSurfaceSnapshot::SecondBatchBehavior::Pending;
		const AshEngine::VegetationBakeResult baked =
			AshEngine::bake_vegetation_chunks(
				TwoDirtyChunkBakeInput(surface),
				VegetationTest::ActiveControl(std::chrono::seconds(5)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
		CHECK(baked.error.find("second batch remains pending") !=
			std::string::npos);
		check_batch_order(*surface);
	}

	SUBCASE("second dirty chunk cancellation discards the completed first chunk")
	{
		auto surface = std::make_shared<BatchedSurfaceSnapshot>();
		auto cancellation = std::make_shared<std::atomic_bool>(false);
		surface->second_batch_behavior = BatchedSurfaceSnapshot::SecondBatchBehavior::Cancel;
		surface->cancellation = cancellation;
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = cancellation;
		control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			TwoDirtyChunkBakeInput(surface), control);
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Cancelled);
		CHECK_FALSE(baked.transaction.has_value());
		check_batch_order(*surface);
	}
}

TEST_CASE("Vegetation baker expired operation exposes no transaction capability")
{
	AshEngine::VegetationOperationControl control{};
	control.cancel_requested = std::make_shared<std::atomic_bool>(false);
	control.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		SingleChunkBakeInput(0x1234u), control);
	CHECK(baked.status == AshEngine::VegetationBakeStatus::TimedOut);
	CHECK_FALSE(baked.transaction.has_value());
}

TEST_CASE("Vegetation baker fails closed outside signed 32-bit height quantization domain")
{
	SUBCASE("value immediately below the upper rounding boundary remains valid")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u);
		auto surface = std::const_pointer_cast<VegetationTest::ScriptedSurfaceSnapshot>(
			std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
				input.surface_snapshot));
		for (AshEngine::VegetationSurfaceSample& sample : surface->result.samples)
		{
			sample.world_height_meters = std::nextafter(
				2147483.6475, -std::numeric_limits<double>::infinity());
		}
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
		const AshEngine::VegetationBakeTransactionOutput& transaction =
			RequireTransaction(baked);
		REQUIRE(transaction.chunks.size() == 1);
		CHECK(transaction.chunks[0].chunk.max_world_height_mm == INT32_MAX);
	}

	SUBCASE("upper ties-to-even boundary fails without partial output")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u);
		auto surface = std::const_pointer_cast<VegetationTest::ScriptedSurfaceSnapshot>(
			std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
				input.surface_snapshot));
		for (AshEngine::VegetationSurfaceSample& sample : surface->result.samples)
		{
			sample.world_height_meters = 2147483.6475;
		}
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
	}

	SUBCASE("value immediately above the upper boundary also fails")
	{
		AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u);
		auto surface = std::const_pointer_cast<VegetationTest::ScriptedSurfaceSnapshot>(
			std::dynamic_pointer_cast<const VegetationTest::ScriptedSurfaceSnapshot>(
				input.surface_snapshot));
		for (AshEngine::VegetationSurfaceSample& sample : surface->result.samples)
		{
			sample.world_height_meters = std::nextafter(
				2147483.6475, std::numeric_limits<double>::infinity());
		}
		const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
			input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
		CHECK_FALSE(baked.transaction.has_value());
	}
}

TEST_CASE("Vegetation baker rejects active entries without species provenance")
{
	AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	auto active = std::const_pointer_cast<AshEngine::VegetationActiveChunkSetSnapshot>(
		ActiveSnapshotFor(input, 7, { 0, 0 }));
	active->entries[0].referenced_species_ids.clear();
	input.active_chunk_set = std::move(active);

	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	CHECK(baked.status == AshEngine::VegetationBakeStatus::Failed);
	CHECK_FALSE(baked.transaction.has_value());
}

TEST_CASE("Vegetation chunk set prepare exposes a move-only immutable capability")
{
	using PrepareFunction = AshEngine::VegetationPreparedChunkSet (*)(
		const std::filesystem::path&,
		const std::filesystem::path&,
		const AshEngine::VegetationBakeResult&,
		AshEngine::VegetationOperationControl,
		AshEngine::VegetationOwnedStageCleanupRegistry&,
		AshEngine::IVegetationImmutablePublishFileOps&);
	static_assert(std::is_same_v<
		decltype(static_cast<PrepareFunction>(
			&AshEngine::prepare_vegetation_chunk_set)),
		PrepareFunction>);
	static_assert(!std::is_copy_constructible_v<
		AshEngine::VegetationPreparedChunkSet>);
	static_assert(!std::is_copy_assignable_v<
		AshEngine::VegetationPreparedChunkSet>);
	static_assert(std::is_move_constructible_v<
		AshEngine::VegetationPreparedChunkSet>);
	static_assert(!std::is_move_assignable_v<
		AshEngine::VegetationPreparedChunkSet>);
	static_assert(!std::is_base_of_v<
		AshEngine::IVegetationCommitFileOps,
		AshEngine::IVegetationImmutablePublishFileOps>);

	const AshEngine::VegetationPreparedChunkSet empty{};
	CHECK(empty.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	CHECK(empty.asset_root().empty());
	CHECK(empty.layer_canonical_relative_path().empty());
	CHECK(empty.layer_resolved_absolute_path().empty());
	CHECK(empty.layer_canonical_identity().empty());
	CHECK(empty.store_canonical_relative_path().empty());
	CHECK(empty.store_resolved_absolute_path().empty());
	CHECK(empty.store_canonical_identity().empty());
	CHECK(empty.active_canonical_relative_path().empty());
	CHECK(empty.active_resolved_absolute_path().empty());
	CHECK(empty.active_canonical_identity().empty());
	CHECK(empty.source_active_identity() ==
		AshEngine::VegetationChunkSetSourceActiveIdentity{});
	CHECK(empty.expected_identity() ==
		AshEngine::VegetationChunkSetExpectedIdentity{});
	CHECK(empty.manifest_sha256() == AshEngine::VegetationSha256{});
	CHECK(empty.stage_path().empty());
	CHECK_FALSE(empty.stage_file_identity().available);
	CHECK(empty.active_stage_size() == 0);
	CHECK(empty.active_stage_sha256() == AshEngine::VegetationSha256{});
	CHECK(empty.error().empty());
}

TEST_CASE("Vegetation chunk set commit exposes the exact main-thread capability API")
{
	using CommitFunction = AshEngine::VegetationChunkSetCommitResult (*)(
		const AshEngine::VegetationPreparedChunkSet&,
		const AshEngine::VegetationChunkSetExpectedIdentity&,
		AshEngine::VegetationOperationControl,
		AshEngine::VegetationOwnedStageCleanupRegistry&,
		AshEngine::IVegetationCommitFileOps&);
	static_assert(std::is_same_v<
		decltype(static_cast<CommitFunction>(
			&AshEngine::commit_vegetation_chunk_set)),
		CommitFunction>);

	const AshEngine::VegetationChunkSetCommitResult result{};
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
	CHECK(result.recovery_path.empty());
	CHECK(result.error.empty());
	CHECK(static_cast<uint8_t>(
		AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired) !=
		static_cast<uint8_t>(AshEngine::VegetationChunkSetCommitStatus::Failed));
}

TEST_CASE("Vegetation chunk set commit rejects capabilities without exact ordinary ownership")
{
	SUBCASE("default capability")
	{
		const AshEngine::VegetationPreparedChunkSet prepared{};
		AshEngine::VegetationOwnedStageCleanupRegistry registry{};
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		const AshEngine::VegetationChunkSetCommitResult result =
			AshEngine::commit_vegetation_chunk_set(
				prepared, {}, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				registry, file_ops);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(file_ops.acquire_call_count == 0);
		CHECK(file_ops.create_new_call_count == 0);
		CHECK(file_ops.atomic_replace_call_count == 0);
		CHECK(file_ops.EventCount("remove-file") == 0);
		CHECK(registry.empty());
	}

	SUBCASE("moved-from capability")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-moved-from");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		AshEngine::VegetationPreparedChunkSet moved(std::move(*fixture.prepared));
		const AshEngine::VegetationChunkSetCommitResult result =
			AshEngine::commit_vegetation_chunk_set(
				*fixture.prepared, fixture.Expected(), fixture.Control(),
				fixture.cleanup_registry, fixture.file_ops);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}

	SUBCASE("wrong registry")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-wrong-registry");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		AshEngine::VegetationOwnedStageCleanupRegistry wrong_registry{};
		const AshEngine::VegetationChunkSetCommitResult result =
			AshEngine::commit_vegetation_chunk_set(
				*fixture.prepared, fixture.Expected(), fixture.Control(),
				wrong_registry, fixture.file_ops);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(wrong_registry.empty());
		CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}

	SUBCASE("recovery-owned stage")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-recovery-owned");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		REQUIRE(fixture.cleanup_registry.RetainStageFileForRecovery(stage));
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		REQUIRE(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}

	SUBCASE("missing registry stage")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-missing-owner");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		REQUIRE(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		fixture.ResetCommitObservations();
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

}

TEST_CASE("Vegetation chunk set commit compares every current expected identity field before lease")
{
	using Mutation = std::function<void(AshEngine::VegetationChunkSetExpectedIdentity&)>;
	const std::vector<std::pair<std::string, Mutation>> mutations = {
		{ "operation serial", [](auto& value) { ++value.operation_serial; } },
		{ "cooker version", [](auto& value) { ++value.cooker_version; } },
		{ "format version", [](auto& value) { ++value.format_version; } },
		{ "layer id", [](auto& value) { ++value.layer_id[0]; } },
		{ "layer generation", [](auto& value) { ++value.layer_generation; } },
		{ "surface id", [](auto& value) { ++value.surface_identity.surface_id[0]; } },
		{ "surface content revision", [](auto& value) {
			++value.surface_identity.content_revision; } },
		{ "surface residency revision", [](auto& value) {
			++value.surface_identity.residency_revision; } },
		{ "surface transform revision", [](auto& value) {
			++value.surface_identity.transform_revision; } },
		{ "species digest", [](auto& value) {
			++value.species_identities.front().canonical_sha256[0]; } },
		{ "target coordinate", [](auto& value) { ++value.target_coords.front().x; } }
	};

	for (size_t index = 0; index < mutations.size(); ++index)
	{
		CAPTURE(mutations[index].first);
		NoActiveCommitFixture fixture(
			"chunk-set-commit-current-identity-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		AshEngine::VegetationChunkSetExpectedIdentity changed = fixture.Expected();
		mutations[index].second(changed);

		const AshEngine::VegetationChunkSetCommitResult result =
			fixture.Commit(changed, fixture.Control());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		if (!fixture.file_ops.events.empty())
		{
			CHECK(fixture.file_ops.events.back().path == stage);
		}
		CHECK(fixture.cleanup_registry.empty());
		std::error_code exists_error{};
		CHECK_FALSE(std::filesystem::exists(stage, exists_error));
		CHECK_FALSE(exists_error);
	}
}

TEST_CASE("Vegetation chunk set commit maps control and lease boundaries exactly")
{
	SUBCASE("malformed control fails before lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-control-malformed");
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit(
			fixture.Expected(), {});
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("pre-cancel maps Cancelled before lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-control-cancelled");
		fixture.cancellation->store(true, std::memory_order_release);
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Cancelled);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("pre-timeout maps TimedOut before lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-control-timeout");
		AshEngine::VegetationOperationControl control = fixture.Control();
		control.deadline = std::chrono::steady_clock::now() -
			std::chrono::milliseconds(1);
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit(
			fixture.Expected(), std::move(control));
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::TimedOut);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("Acquired without a lease payload is illegal")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-lease-missing");
		fixture.file_ops.acquired_lease_without_payload = true;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("non-Acquired with a lease payload is illegal and destroys it")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-lease-extra");
		fixture.file_ops.lease_status = AshEngine::VegetationFileLeaseStatus::Failed;
		fixture.file_ops.non_acquired_lease_with_payload = true;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("legal non-acquired lease statuses map exactly")
	{
		struct LeaseCase
		{
			AshEngine::VegetationFileLeaseStatus lease_status{};
			AshEngine::VegetationChunkSetCommitStatus commit_status{};
		};
		const std::array<LeaseCase, 3> cases = {
			LeaseCase{ AshEngine::VegetationFileLeaseStatus::Cancelled,
				AshEngine::VegetationChunkSetCommitStatus::Cancelled },
			LeaseCase{ AshEngine::VegetationFileLeaseStatus::TimedOut,
				AshEngine::VegetationChunkSetCommitStatus::TimedOut },
			LeaseCase{ AshEngine::VegetationFileLeaseStatus::Failed,
				AshEngine::VegetationChunkSetCommitStatus::Failed }
		};
		for (size_t index = 0; index < cases.size(); ++index)
		{
			CAPTURE(index);
			NoActiveCommitFixture fixture(
				"chunk-set-commit-lease-status-" + std::to_string(index));
			fixture.file_ops.lease_status = cases[index].lease_status;
			const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
			CHECK(result.status == cases[index].commit_status);
			CHECK(fixture.file_ops.acquire_call_count == 1);
			CHECK(*fixture.file_ops.lease_destruction_count == 0);
			CHECK(fixture.file_ops.create_new_call_count == 0);
			CHECK(fixture.cleanup_registry.empty());
		}
	}

	SUBCASE("cancellation immediately after acquire releases the lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-cancel-after-acquire");
		fixture.file_ops.after_lease_result = [&fixture]()
		{
			fixture.cancellation->store(true, std::memory_order_release);
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Cancelled);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.inspection_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("cancellation after active CAS releases the lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-cancel-after-active");
		fixture.file_ops.cancel_after_inspection_call = 3;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Cancelled);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.inspection_call_count == 3);
		CHECK(fixture.file_ops.read_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("cancellation after stage read releases the lease")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-cancel-after-stage");
		fixture.file_ops.cancel_after_read_call = 1;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Cancelled);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.inspection_call_count == 4);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit rechecks exact Layer and store capabilities inside the lease")
{
	using InspectionMutation =
		std::function<void(AshEngine::VegetationFileInspection&)>;
	struct IdentityCase
	{
		std::string name{};
		bool layer = true;
		InspectionMutation mutate{};
	};
	const std::vector<IdentityCase> cases = {
		{ "layer canonical relative", true, [](auto& value) {
			value.canonical_relative_path = "vegetation/other.AshVegetationLayer"; } },
		{ "layer resolved absolute", true, [](auto& value) {
			value.resolved_absolute_path =
				(value.resolved_absolute_path.parent_path() / "other.AshVegetationLayer").lexically_normal(); } },
		{ "layer canonical identity", true, [](auto& value) {
			value.canonical_identity += "-changed"; } },
		{ "layer native identity", true, [](auto& value) {
			++value.file_identity.file_index; } },
		{ "store canonical relative", false, [](auto& value) {
			value.canonical_relative_path /= "other"; } },
		{ "store resolved absolute", false, [](auto& value) {
			value.resolved_absolute_path =
				(value.resolved_absolute_path / "other").lexically_normal(); } },
		{ "store canonical identity", false, [](auto& value) {
			value.canonical_identity += "-changed"; } },
		{ "store native identity", false, [](auto& value) {
			++value.file_identity.file_index; } }
	};

	for (size_t index = 0; index < cases.size(); ++index)
	{
		CAPTURE(cases[index].name);
		NoActiveCommitFixture fixture(
			"chunk-set-commit-path-capability-" + std::to_string(index));
		const std::filesystem::path target = cases[index].layer
			? fixture.prepared->layer_canonical_relative_path()
			: fixture.prepared->store_canonical_relative_path();
		fixture.file_ops.inspection_result_hook =
			[target, mutate = cases[index].mutate](
				const std::filesystem::path& path,
				AshEngine::VegetationFileInspection& inspection)
			{
				if (path == target)
				{
					mutate(inspection);
				}
			};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
		std::error_code exists_error{};
		CHECK_FALSE(std::filesystem::exists(fixture.ActiveAbsolute(), exists_error));
		CHECK_FALSE(exists_error);
	}

	SUBCASE("same Layer path delete and recreate changes the native identity")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-layer-recreate");
		AshEngine::IVegetationFileOps& backing =
			AshEngine::get_default_vegetation_file_ops();
		const AshEngine::VegetationFileInspection before = backing.InspectPath(
			fixture.root.Path(), fixture.source.layer_relative_path);
		REQUIRE(before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(before.file_identity.available);
		const AshEngine::VegetationFileBytesResult bytes = backing.ReadAllBytes(
			before.resolved_absolute_path, std::numeric_limits<uint64_t>::max());
		REQUIRE(bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
		std::error_code remove_error{};
		REQUIRE(std::filesystem::remove(before.resolved_absolute_path, remove_error));
		REQUIRE_FALSE(remove_error);
		WritePrepareBytes(before.resolved_absolute_path, bytes.bytes);
		const AshEngine::VegetationFileInspection after = backing.InspectPath(
			fixture.root.Path(), fixture.source.layer_relative_path);
		REQUIRE(after.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(after.file_identity.available);
		const bool native_identity_changed =
			before.file_identity.volume_serial_number !=
				after.file_identity.volume_serial_number ||
			before.file_identity.file_index != after.file_identity.file_index;
		REQUIRE(native_identity_changed);

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit fails closed on active inspection shape and identity drift")
{
	enum class ActiveFault : uint8_t
	{
		IllegalFailedPayload,
		LegalFailed,
		CanonicalRelative,
		ResolvedAbsolute,
		CanonicalIdentity
	};
	const std::array<std::pair<const char*, ActiveFault>, 5> cases = {
		std::pair{ "illegal failed payload", ActiveFault::IllegalFailedPayload },
		std::pair{ "legal Failed", ActiveFault::LegalFailed },
		std::pair{ "canonical relative drift", ActiveFault::CanonicalRelative },
		std::pair{ "resolved absolute drift", ActiveFault::ResolvedAbsolute },
		std::pair{ "canonical identity drift", ActiveFault::CanonicalIdentity }
	};

	for (size_t index = 0; index < cases.size(); ++index)
	{
		CAPTURE(cases[index].first);
		NoActiveCommitFixture fixture(
			"chunk-set-commit-active-inspect-" + std::to_string(index));
		const ActiveFault fault = cases[index].second;
		const std::filesystem::path active_relative =
			fixture.prepared->active_canonical_relative_path();
		fixture.file_ops.inspection_result_hook =
			[active_relative, fault](const std::filesystem::path& path,
				AshEngine::VegetationFileInspection& inspection)
			{
				if (path != active_relative)
				{
					return;
				}
				switch (fault)
				{
				case ActiveFault::IllegalFailedPayload:
					inspection.status = AshEngine::VegetationFileResultStatus::Failed;
					break;
				case ActiveFault::LegalFailed:
					inspection = {};
					break;
				case ActiveFault::CanonicalRelative:
					inspection.canonical_relative_path /= "other.asva";
					break;
				case ActiveFault::ResolvedAbsolute:
					inspection.resolved_absolute_path =
						(inspection.resolved_absolute_path.parent_path() /
							"other.asva").lexically_normal();
					break;
				case ActiveFault::CanonicalIdentity:
					inspection.canonical_identity += "-changed";
					break;
				}
			};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.inspection_call_count == 3);
		CHECK(fixture.file_ops.read_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit rejects an illegal stage byte result shape")
{
	NoActiveCommitFixture fixture("chunk-set-commit-stage-read-shape");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	fixture.file_ops.read_result_hook =
		[stage](const std::filesystem::path& path, const uint64_t max_bytes,
			AshEngine::VegetationFileBytesResult& bytes)
		{
			if (path == stage)
			{
				CHECK(max_bytes == 48);
				REQUIRE(bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
				bytes.status = AshEngine::VegetationFileResultStatus::Failed;
			}
		};

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
	CHECK(result.recovery_path.empty());
	CHECK(fixture.file_ops.read_call_count == 1);
	CHECK(fixture.file_ops.create_new_call_count == 0);
	CHECK(fixture.file_ops.atomic_replace_call_count == 0);
	CHECK(fixture.cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set commit keeps its prepared absolute root across a CWD change")
{
	NoActiveCommitFixture fixture("chunk-set-commit-bound-root");
	VegetationTest::ScopedAssetRoot unrelated("chunk-set-commit-unrelated-cwd");
	ScopedCurrentPathRestore current_path{};
	current_path.Set(unrelated.Path());

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
	CHECK(result.recovery_path.empty());
	REQUIRE(fixture.file_ops.root_arguments.size() == 4);
	for (const std::filesystem::path& root : fixture.file_ops.root_arguments)
	{
		CHECK(root == fixture.prepared->asset_root());
	}
	CHECK(fixture.file_ops.last_create_target == fixture.ActiveAbsolute());
	CHECK(fixture.file_ops.atomic_replace_call_count == 0);
}

TEST_CASE("Vegetation chunk set commit publishes NoActive only through create-new CAS")
{
	SUBCASE("Created consumes the exact stage and publishes the prepared manifest")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-no-active-created");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::vector<uint8_t> expected_active = EncodeActivePointerOrThrow(
			fixture.prepared->manifest_sha256());
		const AshEngine::VegetationOperationControl control = fixture.Control();
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit(
			fixture.Expected(), control);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(result.error.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(fixture.file_ops.last_lease_identity ==
			fixture.prepared->store_canonical_identity());
		CHECK(fixture.file_ops.last_lease_cancel_requested ==
			control.cancel_requested);
		CHECK(fixture.file_ops.last_lease_deadline == control.deadline);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.last_create_stage == stage);
		CHECK(fixture.file_ops.last_create_target == fixture.ActiveAbsolute());
		CHECK(fixture.cleanup_registry.empty());
		CHECK_FALSE(fixture.cleanup_registry.OwnsStageFile(stage));

		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == expected_active);
		AshEngine::VegetationChunkSetActivePointer decoded{};
		std::string error{};
		REQUIRE(AshEngine::decode_vegetation_chunk_set_active_pointer(
			active.bytes, decoded, &error));
		CHECK(decoded.manifest_sha256 == fixture.prepared->manifest_sha256());
		std::error_code exists_error{};
		CHECK_FALSE(std::filesystem::exists(stage, exists_error));
		CHECK_FALSE(exists_error);
	}

	SUBCASE("authoritative active reread sees a winner before publication")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-no-active-appeared");
		AshEngine::VegetationSha256 winner_manifest{};
		winner_manifest.fill(0x61u);
		const std::vector<uint8_t> winner =
			EncodeActivePointerOrThrow(winner_manifest);
		WritePrepareBytes(fixture.ActiveAbsolute(), winner);

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == winner);
	}

	SUBCASE("create-new race reports AlreadyExists and preserves the exact winner")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-no-active-race");
		AshEngine::VegetationSha256 winner_manifest{};
		winner_manifest.fill(0x72u);
		const std::vector<uint8_t> winner =
			EncodeActivePointerOrThrow(winner_manifest);
		fixture.file_ops.create_new = [&winner](
			const std::filesystem::path&,
			const std::filesystem::path& target)
		{
			WritePrepareBytes(target, winner);
			return AshEngine::VegetationCreateNewStatus::AlreadyExists;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::AlreadyExists);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.create_new_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == winner);
	}

	SUBCASE("create-new failure leaves active absent")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-no-active-failed");
		fixture.file_ops.create_new = [](
			const std::filesystem::path&,
			const std::filesystem::path&)
		{
			return AshEngine::VegetationCreateNewStatus::Failed;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.create_new_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		std::error_code exists_error{};
		CHECK_FALSE(std::filesystem::exists(fixture.ActiveAbsolute(), exists_error));
		CHECK_FALSE(exists_error);
	}
}

TEST_CASE("Vegetation chunk set commit strictly revalidates the exact 48-byte active stage")
{
	enum class StageFault : uint8_t
	{
		Missing,
		Grown,
		SameSizeBitFlip,
		CrcFlip,
		NativeIdentityMismatch,
		TargetAlias
	};
	const std::array<std::pair<const char*, StageFault>, 6> cases = {
		std::pair{ "missing", StageFault::Missing },
		std::pair{ "grown", StageFault::Grown },
		std::pair{ "same-size bit flip", StageFault::SameSizeBitFlip },
		std::pair{ "CRC flip", StageFault::CrcFlip },
		std::pair{ "native identity mismatch", StageFault::NativeIdentityMismatch },
		std::pair{ "target alias", StageFault::TargetAlias }
	};

	for (size_t index = 0; index < cases.size(); ++index)
	{
		CAPTURE(cases[index].first);
		NoActiveCommitFixture fixture(
			"chunk-set-commit-stage-fault-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path stage_relative =
			(fixture.prepared->active_canonical_relative_path().parent_path() /
				stage.filename()).lexically_normal();
		const AshEngine::VegetationFileBytesResult original =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		REQUIRE(original.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(original.bytes.size() == 48);

		switch (cases[index].second)
		{
		case StageFault::Missing:
			REQUIRE(AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
				stage, fixture.prepared->stage_file_identity()));
			break;
		case StageFault::Grown:
		{
			std::vector<uint8_t> grown = original.bytes;
			grown.push_back(0x5au);
			WritePrepareBytes(stage, grown);
			break;
		}
		case StageFault::SameSizeBitFlip:
		{
			std::vector<uint8_t> changed = original.bytes;
			changed[12] ^= 0x80u;
			WritePrepareBytes(stage, changed);
			break;
		}
		case StageFault::CrcFlip:
		{
			std::vector<uint8_t> changed = original.bytes;
			changed.back() ^= 0x01u;
			WritePrepareBytes(stage, changed);
			break;
		}
		case StageFault::NativeIdentityMismatch:
			fixture.file_ops.inspection_result_hook =
				[stage_relative](const std::filesystem::path& path,
					AshEngine::VegetationFileInspection& inspection)
				{
					if (path == stage_relative)
					{
						++inspection.file_identity.file_index;
					}
				};
			break;
		case StageFault::TargetAlias:
			fixture.file_ops.inspection_result_hook =
				[&fixture, stage_relative](const std::filesystem::path& path,
					AshEngine::VegetationFileInspection& inspection)
				{
					if (path == stage_relative)
					{
						inspection.canonical_relative_path =
							fixture.prepared->active_canonical_relative_path();
						inspection.resolved_absolute_path =
							fixture.prepared->active_resolved_absolute_path();
						inspection.canonical_identity =
							fixture.prepared->active_canonical_identity();
					}
				};
			break;
		}

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
		std::error_code exists_error{};
		CHECK_FALSE(std::filesystem::exists(fixture.ActiveAbsolute(), exists_error));
		CHECK_FALSE(exists_error);
	}
}

TEST_CASE("Vegetation chunk set commit success is terminal and the capability is single-use")
{
	NoActiveCommitFixture fixture("chunk-set-commit-single-use");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	const AshEngine::VegetationChunkSetCommitResult first = fixture.Commit();
	REQUIRE(first.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
	REQUIRE(fixture.cleanup_registry.empty());
	const AshEngine::VegetationFileBytesResult winner =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			fixture.ActiveAbsolute(), 48);
	REQUIRE(winner.status == AshEngine::VegetationFileResultStatus::Succeeded);

	fixture.ResetCommitObservations();
	const AshEngine::VegetationChunkSetCommitResult second = fixture.Commit();
	CHECK(second.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
	CHECK(fixture.file_ops.acquire_call_count == 0);
	CHECK(fixture.file_ops.create_new_call_count == 0);
	CHECK(fixture.file_ops.atomic_replace_call_count == 0);
	CHECK(fixture.file_ops.EventCount("remove-file") == 0);
	CHECK(fixture.cleanup_registry.empty());
	const AshEngine::VegetationFileBytesResult after =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			fixture.ActiveAbsolute(), 48);
	REQUIRE(after.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(after.bytes == winner.bytes);
	std::error_code exists_error{};
	CHECK_FALSE(std::filesystem::exists(stage, exists_error));
	CHECK_FALSE(exists_error);
}

TEST_CASE("Vegetation chunk set commit does not downgrade a created pointer after late cancellation or bookkeeping")
{
	NoActiveCommitFixture fixture("chunk-set-commit-terminal-created");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	fixture.file_ops.create_new = [&fixture](
		const std::filesystem::path& source,
		const std::filesystem::path& target)
	{
		const AshEngine::VegetationCreateNewStatus status =
			AshEngine::get_default_vegetation_file_ops().CreateNewFromStage(
				source, target);
		if (status == AshEngine::VegetationCreateNewStatus::Created)
		{
			fixture.cancellation->store(true, std::memory_order_release);
			(void)fixture.cleanup_registry.ForgetConsumedStageFile(source);
		}
		return status;
	};

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
	CHECK(fixture.cancellation->load(std::memory_order_acquire));
	CHECK(fixture.file_ops.create_new_call_count == 1);
	CHECK(fixture.file_ops.atomic_replace_call_count == 0);
	CHECK(fixture.cleanup_registry.empty());
	std::error_code exists_error{};
	CHECK_FALSE(std::filesystem::exists(stage, exists_error));
	CHECK_FALSE(exists_error);
	const AshEngine::VegetationFileBytesResult active =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			fixture.ActiveAbsolute(), 48);
	REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationChunkSetActivePointer decoded{};
	std::string error{};
	REQUIRE(AshEngine::decode_vegetation_chunk_set_active_pointer(
		active.bytes, decoded, &error));
	CHECK(decoded.manifest_sha256 == fixture.prepared->manifest_sha256());
}

TEST_CASE("Vegetation chunk set commit contains FileOps exceptions before publication")
{
	const auto check_prepublication_exception = [](
		NoActiveCommitFixture& fixture,
		const AshEngine::VegetationChunkSetCommitResult& result,
		const std::filesystem::path& stage,
		const size_t expected_inspections,
		const size_t expected_reads,
		const size_t expected_create_new_calls)
	{
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(result.error.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.inspection_call_count == expected_inspections);
		CHECK(fixture.file_ops.read_call_count == expected_reads);
		CHECK(fixture.file_ops.create_new_call_count == expected_create_new_calls);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		const auto removal = std::find_if(
			fixture.file_ops.events.begin(), fixture.file_ops.events.end(),
			[](const PrepareFileOpEvent& event)
			{
				return event.name == "remove-file";
			});
		REQUIRE(removal != fixture.file_ops.events.end());
		CHECK(removal->path == stage);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult removed_stage =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed_stage.status == AshEngine::VegetationFileResultStatus::NotFound);
		CHECK(removed_stage.bytes.empty());
		const AshEngine::VegetationFileBytesResult unchanged_active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(unchanged_active.status == AshEngine::VegetationFileResultStatus::NotFound);
		CHECK(unchanged_active.bytes.empty());
	};

	SUBCASE("AcquireNamedLease direct exception before payload is contained")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-acquire-direct");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.throw_acquire = true;
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(result.error.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 0);
		CHECK(fixture.file_ops.inspection_call_count == 0);
		CHECK(fixture.file_ops.read_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult removed_stage =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed_stage.status == AshEngine::VegetationFileResultStatus::NotFound);
		const AshEngine::VegetationFileBytesResult unchanged_active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(unchanged_active.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("AcquireNamedLease exception is contained")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-acquire");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.after_lease_result = []()
		{
			throw std::runtime_error("injected commit lease exception");
		};
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		check_prepublication_exception(fixture, result, stage, 0, 0, 0);
	}

	SUBCASE("lease-held inspection exception is contained")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-inspect");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.throw_inspection_call = 1;
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		check_prepublication_exception(fixture, result, stage, 1, 0, 0);
	}

	SUBCASE("stage read exception is contained")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-read");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.fault_rules.push_back(
			{ "read", 1, PrepareFaultMode::Throw });
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		check_prepublication_exception(fixture, result, stage, 4, 1, 0);
	}

	SUBCASE("CreateNewFromStage exception is contained without mutating active")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-create");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.create_new = [](
			const std::filesystem::path&,
			const std::filesystem::path&) -> AshEngine::VegetationCreateNewStatus
		{
			throw std::runtime_error("injected commit create-new exception");
		};
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		// Exception outcome proof adds one post-call inspection.
		check_prepublication_exception(fixture, result, stage, 5, 1, 1);
	}

	SUBCASE("CreateNewFromStage terminal creation is recovered after its callback throws")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-after-created");
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path stage_relative =
			(fixture.prepared->active_canonical_relative_path().parent_path() /
				stage.filename()).lexically_normal();
		const AshEngine::VegetationFileInspection stage_before =
			default_ops.InspectPath(fixture.root.Path(), stage_relative);
		REQUIRE(stage_before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(stage_before.exists);
		REQUIRE(stage_before.file_identity.available);
		const std::vector<uint8_t> expected_active = EncodeActivePointerOrThrow(
			fixture.prepared->manifest_sha256());
		fixture.file_ops.create_new = [&default_ops](
			const std::filesystem::path& source,
			const std::filesystem::path& target) -> AshEngine::VegetationCreateNewStatus
		{
			if (default_ops.CreateNewFromStage(source, target) !=
				AshEngine::VegetationCreateNewStatus::Created)
			{
				throw std::runtime_error("default create-new did not publish");
			}
			throw std::runtime_error("injected exception after terminal create-new");
		};

		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(result.error.empty());
		CHECK(fixture.file_ops.create_new_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileInspection active_after =
			default_ops.InspectPath(
				fixture.root.Path(), fixture.prepared->active_canonical_relative_path());
		REQUIRE(active_after.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(active_after.exists);
		REQUIRE(active_after.is_regular_file);
		REQUIRE(active_after.file_identity.available);
		CHECK(active_after.file_identity.volume_serial_number ==
			stage_before.file_identity.volume_serial_number);
		CHECK(active_after.file_identity.file_index ==
			stage_before.file_identity.file_index);
		const AshEngine::VegetationFileBytesResult active_bytes =
			default_ops.ReadAllBytes(fixture.ActiveAbsolute(), 48);
		REQUIRE(active_bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active_bytes.bytes == expected_active);
		CHECK_FALSE(std::filesystem::exists(stage));
	}

	SUBCASE("unrelated exact-byte target cannot prove terminal create-new success")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-decoy-created");
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path stage_relative =
			(fixture.prepared->active_canonical_relative_path().parent_path() /
				stage.filename()).lexically_normal();
		const AshEngine::VegetationFileInspection stage_before =
			default_ops.InspectPath(fixture.root.Path(), stage_relative);
		REQUIRE(stage_before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(stage_before.file_identity.available);
		const std::vector<uint8_t> expected_active = EncodeActivePointerOrThrow(
			fixture.prepared->manifest_sha256());
		AshEngine::VegetationStageFileResult decoy =
			default_ops.CreateUniqueSiblingStageFile(fixture.ActiveAbsolute(), 0xdec0u);
		REQUIRE(decoy.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(decoy.file_identity.available);
		REQUIRE(decoy.writer->WriteBlock(
			0, { expected_active.data(), expected_active.size() }));
		REQUIRE(decoy.writer->FlushAndClose());
		decoy.writer.reset();
		const std::filesystem::path decoy_path = decoy.owned_stage_file;
		const AshEngine::VegetationFileIdentity decoy_identity = decoy.file_identity;
		fixture.file_ops.create_new =
			[&fixture, &default_ops, decoy_path, stage_before](
				const std::filesystem::path& source,
				const std::filesystem::path& target) -> AshEngine::VegetationCreateNewStatus
		{
			if (!default_ops.RemoveOwnedStageFile(
					source, stage_before.file_identity) ||
				default_ops.CreateNewFromStage(decoy_path, target) !=
					AshEngine::VegetationCreateNewStatus::Created ||
				!fixture.cleanup_registry.ForgetConsumedStageFile(source))
			{
				throw std::runtime_error("could not establish unrelated active target");
			}
			throw std::runtime_error("injected exception after unrelated active creation");
		};

		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.status != AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileInspection active_after =
			default_ops.InspectPath(
				fixture.root.Path(), fixture.prepared->active_canonical_relative_path());
		REQUIRE(active_after.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(active_after.exists);
		REQUIRE(active_after.file_identity.available);
		CHECK(active_after.file_identity.volume_serial_number ==
			decoy_identity.volume_serial_number);
		CHECK(active_after.file_identity.file_index == decoy_identity.file_index);
		CHECK((active_after.file_identity.volume_serial_number !=
			stage_before.file_identity.volume_serial_number ||
			active_after.file_identity.file_index != stage_before.file_identity.file_index));
		const AshEngine::VegetationFileBytesResult active_bytes =
			default_ops.ReadAllBytes(fixture.ActiveAbsolute(), 48);
		REQUIRE(active_bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active_bytes.bytes == expected_active);
		CHECK_FALSE(std::filesystem::exists(stage));
	}

	SUBCASE("cleanup exception is contained and retains the exact stage for retry")
	{
		NoActiveCommitFixture fixture("chunk-set-commit-throw-cleanup");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.after_lease_result = []()
		{
			throw std::runtime_error("injected commit lease exception");
		};
		fixture.file_ops.fault_rules.push_back(
			{ "remove-file", 1, PrepareFaultMode::Throw });
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		const AshEngine::VegetationFileBytesResult retained =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained.bytes.size() == 48);
		const AshEngine::VegetationFileBytesResult unchanged_active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(unchanged_active.status == AshEngine::VegetationFileResultStatus::NotFound);

		fixture.file_ops.fault_rules.clear();
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit cleanup failure retains only the exact stage and reports Failed")
{
	NoActiveCommitFixture fixture("chunk-set-commit-cleanup-failed");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	fixture.cancellation->store(true, std::memory_order_release);
	fixture.file_ops.fault_rules.push_back(
		{ "remove-file", 1, PrepareFaultMode::ReturnFalse });

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
	CHECK(result.recovery_path.empty());
	CHECK(fixture.file_ops.acquire_call_count == 0);
	CHECK(fixture.file_ops.create_new_call_count == 0);
	CHECK(fixture.file_ops.atomic_replace_call_count == 0);
	CHECK(fixture.file_ops.EventCount("remove-file") == 1);
	if (!fixture.file_ops.events.empty())
	{
		CHECK(fixture.file_ops.events.back().path == stage);
	}
	CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
	CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
	const AshEngine::VegetationFileBytesResult retained =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
	CHECK(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(retained.bytes.size() == 48);

	fixture.file_ops.fault_rules.clear();
	CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	CHECK(fixture.cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set commit validates Existing active under lease and maps atomic replace outcomes")
{
	SUBCASE("matching Existing source atomically replaces the active pointer")
	{
		ExistingCommitFixture fixture("chunk-set-existing-replaced");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::vector<uint8_t> expected_active =
			EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256());
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			return AshEngine::get_default_vegetation_file_ops().AtomicReplace(
				source, target, registry);
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.last_atomic_stage == stage);
		CHECK(fixture.file_ops.last_atomic_target == fixture.ActiveAbsolute());
		std::vector<PrepareFileOpEvent> reads{};
		std::copy_if(fixture.file_ops.events.begin(), fixture.file_ops.events.end(),
			std::back_inserter(reads), [](const PrepareFileOpEvent& event)
			{
				return event.name == "read";
			});
		CHECK(reads.size() == 2);
		if (reads.size() == 2)
		{
			CHECK(reads[0].path == fixture.ActiveAbsolute());
			CHECK(reads[0].value == 48);
			CHECK(reads[1].path == stage);
			CHECK(reads[1].value == 48);
		}
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == expected_active);
		const AshEngine::VegetationFileBytesResult consumed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(consumed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("TargetPreserved reports Failed and cleans only the staged replacement")
	{
		ExistingCommitFixture fixture("chunk-set-existing-preserved");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::vector<uint8_t> old_active = fixture.source.source_active_bytes;
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::TargetPreserved;
			result.error = "injected preserved target";
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == old_active);
		const AshEngine::VegetationFileBytesResult removed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("valid RecoveryRequired exposes one exact protected recovery path")
	{
		ExistingCommitFixture fixture("chunk-set-existing-recovery");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			if (!registry.BeginStageFilePublish(source, target) ||
				!registry.ResolveStageFilePublish(
					source,
					AshEngine::VegetationStageFilePublishResolution::RecoveryRequired))
			{
				throw std::runtime_error("could not inject associated stage recovery");
			}
			result.recovery_path = source;
			result.error = "injected recovery";
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.bytes == fixture.source.source_active_bytes);
		const AshEngine::VegetationFileBytesResult retained =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}
}

TEST_CASE("Vegetation chunk set commit rereads Existing active only after acquiring its lease")
{
	SUBCASE("a legal manifest change immediately after acquire is SourceChanged")
	{
		ExistingCommitFixture fixture("chunk-set-existing-after-lease-drift");
		AshEngine::VegetationSha256 other{};
		other.fill(0x5c);
		REQUIRE(other != fixture.source.source_manifest_sha256);
		fixture.file_ops.after_lease_result = [&fixture, other]()
		{
			WritePrepareBytes(
				fixture.ActiveAbsolute(), EncodeActivePointerOrThrow(other));
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 1);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("active disappearing between inspection and bounded read is SourceChanged")
	{
		ExistingCommitFixture fixture("chunk-set-existing-read-not-found");
		fixture.file_ops.overridden_read_call = 1;
		fixture.file_ops.read_override.status =
			AshEngine::VegetationFileResultStatus::NotFound;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.inspection_call_count == 3);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit accepts a distinct protected atomic-replace recovery artifact")
{
	ExistingCommitFixture fixture("chunk-set-existing-distinct-recovery");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	AshEngine::IVegetationFileOps& default_ops =
		AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path backup =
		(fixture.ActiveAbsolute().parent_path() /
			L".ashveg-layer-stage-replace-backup-test.tmp").lexically_normal();
	REQUIRE(backup.is_absolute());
	REQUIRE(backup != stage);
	const AshEngine::VegetationFileInspection target_before_recovery =
		default_ops.InspectPath(
			fixture.root.Path(),
			fixture.prepared->active_canonical_relative_path());
	REQUIRE(target_before_recovery.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(target_before_recovery.file_identity.available);
	REQUIRE(CreateHardLinkW(
		ExtendedPrepareWindowsPath(backup).c_str(),
		ExtendedPrepareWindowsPath(fixture.ActiveAbsolute()).c_str(), nullptr) != FALSE);
	const AshEngine::VegetationFileInspection backup_before_recovery =
		default_ops.InspectPath(
			fixture.root.Path(), backup.lexically_relative(fixture.root.Path()));
	REQUIRE(backup_before_recovery.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(backup_before_recovery.file_identity.available);
	REQUIRE(backup_before_recovery.file_identity.volume_serial_number ==
		target_before_recovery.file_identity.volume_serial_number);
	REQUIRE(backup_before_recovery.file_identity.file_index ==
		target_before_recovery.file_identity.file_index);
	fixture.file_ops.atomic_replace = [backup, target_before_recovery](
		const std::filesystem::path& source,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		if (!registry.BeginStageFilePublish(source, target) ||
			!registry.RetainStageFileForAtomicReplaceRecovery(
				backup, source, target, target_before_recovery.file_identity))
		{
			throw std::runtime_error("could not inject associated backup recovery");
		}
		if (!registry.ResolveStageFilePublish(
				source,
				AshEngine::VegetationStageFilePublishResolution::TargetPreserved))
		{
			throw std::runtime_error("could not capture associated backup recovery");
		}
		AshEngine::VegetationAtomicReplaceResult result{};
		result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		result.recovery_path = backup;
		result.error = "injected distinct recovery backup";
		return result;
	};

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
	CHECK(result.recovery_path == backup);
	CHECK(fixture.file_ops.atomic_replace_call_count == 1);
	CHECK(fixture.file_ops.create_new_call_count == 0);
	CHECK(fixture.file_ops.EventCount("remove-file") == 0);
	CHECK(fixture.cleanup_registry.IsRecoveryStageFile(backup));
	CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
	CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));

	CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(backup));
	CHECK(fixture.cleanup_registry.CleanupStageFile(backup, fixture.file_ops));
	CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	CHECK(fixture.cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set commit rejects same-byte recovery backup identity drift")
{
	ExistingCommitFixture fixture("chunk-set-existing-recovery-identity-drift");
	AshEngine::IVegetationFileOps& default_ops =
		AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path stage = fixture.prepared->stage_path();
	const std::filesystem::path backup =
		(fixture.ActiveAbsolute().parent_path() /
			L".ashveg-layer-stage-replace-backup-identity-drift.tmp").lexically_normal();
	const AshEngine::VegetationFileInspection target_before_recovery =
		default_ops.InspectPath(
			fixture.root.Path(),
			fixture.prepared->active_canonical_relative_path());
	REQUIRE(target_before_recovery.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(target_before_recovery.file_identity.available);
	REQUIRE(CreateHardLinkW(
		ExtendedPrepareWindowsPath(backup).c_str(),
		ExtendedPrepareWindowsPath(fixture.ActiveAbsolute()).c_str(), nullptr) != FALSE);
	const AshEngine::VegetationFileInspection backup_before_recovery =
		default_ops.InspectPath(
			fixture.root.Path(), backup.lexically_relative(fixture.root.Path()));
	REQUIRE(backup_before_recovery.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(backup_before_recovery.file_identity.available);
	REQUIRE(backup_before_recovery.file_identity.volume_serial_number ==
		target_before_recovery.file_identity.volume_serial_number);
	REQUIRE(backup_before_recovery.file_identity.file_index ==
		target_before_recovery.file_identity.file_index);

	AshEngine::VegetationStageFileResult same_bytes_replacement =
		default_ops.CreateUniqueSiblingStageFile(fixture.ActiveAbsolute(), 0x730101u);
	REQUIRE(same_bytes_replacement.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(same_bytes_replacement.file_identity.available);
	REQUIRE(same_bytes_replacement.writer->WriteBlock(
		0, { fixture.source.source_active_bytes.data(),
			fixture.source.source_active_bytes.size() }));
	REQUIRE(same_bytes_replacement.writer->FlushAndClose());
	same_bytes_replacement.writer.reset();
	REQUIRE((same_bytes_replacement.file_identity.volume_serial_number !=
		target_before_recovery.file_identity.volume_serial_number ||
		same_bytes_replacement.file_identity.file_index !=
			target_before_recovery.file_identity.file_index));

	bool replacement_injected = false;
	fixture.file_ops.atomic_replace =
		[&, backup, target_before_recovery](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		if (!registry.BeginStageFilePublish(source, target) ||
			!registry.RetainStageFileForAtomicReplaceRecovery(
				backup, source, target, target_before_recovery.file_identity))
		{
			throw std::runtime_error("could not register associated recovery backup");
		}
		if (!registry.ResolveStageFilePublish(
				source,
				AshEngine::VegetationStageFilePublishResolution::TargetPreserved))
		{
			throw std::runtime_error("could not resolve associated recovery source");
		}
		if (!default_ops.RemoveOwnedStageFile(
				backup, target_before_recovery.file_identity))
		{
			throw std::runtime_error("could not remove captured recovery backup");
		}
		if (MoveFileExW(
				ExtendedPrepareWindowsPath(
					same_bytes_replacement.owned_stage_file).c_str(),
				ExtendedPrepareWindowsPath(backup).c_str(),
				MOVEFILE_WRITE_THROUGH) == FALSE)
		{
			throw std::runtime_error("could not inject recovery backup identity drift");
		}
		replacement_injected = true;
		AshEngine::VegetationAtomicReplaceResult result{};
		result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		result.recovery_path = backup;
		result.error = "injected recovery backup identity drift";
		return result;
	};

	const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
	CHECK(replacement_injected);
	INFO(result.error);
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
	CHECK(result.recovery_path == stage);
	CHECK(result.recovery_path != backup);
	CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
	CHECK_FALSE(fixture.cleanup_registry.OwnsStageFile(backup));
	const AshEngine::VegetationFileInspection replacement_after =
		default_ops.InspectPath(
			fixture.root.Path(), backup.lexically_relative(fixture.root.Path()));
	REQUIRE(replacement_after.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(replacement_after.file_identity.available);
	CHECK(replacement_after.file_identity.volume_serial_number ==
		same_bytes_replacement.file_identity.volume_serial_number);
	CHECK(replacement_after.file_identity.file_index ==
		same_bytes_replacement.file_identity.file_index);
	const AshEngine::VegetationFileBytesResult replacement_bytes =
		default_ops.ReadAllBytes(backup, fixture.source.source_active_bytes.size());
	REQUIRE(replacement_bytes.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(replacement_bytes.bytes == fixture.source.source_active_bytes);

	if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
	{
		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
	}
	if (fixture.cleanup_registry.OwnsStageFile(stage))
	{
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}
	if (fixture.cleanup_registry.IsRecoveryStageFile(backup))
	{
		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(backup));
	}
	if (fixture.cleanup_registry.OwnsStageFile(backup))
	{
		CHECK(fixture.cleanup_registry.ForgetConsumedStageFile(backup));
	}
	CHECK(default_ops.RemoveOwnedStageFile(
		backup, same_bytes_replacement.file_identity));
	CHECK(fixture.cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set commit rejects recovery artifacts without its exact atomic provenance")
{
	SUBCASE("generic recovery falls back only to the intact prepared stage")
	{
		ExistingCommitFixture fixture("chunk-set-existing-generic-recovery-decoy");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path target = fixture.ActiveAbsolute();
		const std::filesystem::path decoy =
			(target.parent_path() /
				L".ashveg-layer-stage-generic-recovery-decoy.tmp").lexically_normal();
		WritePrepareBytes(decoy, fixture.source.source_active_bytes);
		const AshEngine::VegetationFileInspection decoy_inspection =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				fixture.root.Path(), decoy.lexically_relative(fixture.root.Path()));
		REQUIRE(decoy_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(decoy_inspection.file_identity.available);
		REQUIRE(fixture.cleanup_registry.TrackNewRecoveryStageFile(
			decoy, decoy_inspection.file_identity));
		fixture.file_ops.atomic_replace = [decoy](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = decoy;
			result.error = "injected generic recovery decoy";
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(decoy));
		CHECK_FALSE(fixture.cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			decoy, stage, target, decoy_inspection.file_identity));
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		if (fixture.cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		}
		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(decoy));
		CHECK(fixture.cleanup_registry.CleanupStageFile(decoy, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("another operation recovery falls back only to the intact prepared stage")
	{
		ExistingCommitFixture fixture("chunk-set-existing-other-recovery-decoy");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path target = fixture.ActiveAbsolute();
		const std::filesystem::path other_source =
			(target.parent_path() /
				L".ashveg-layer-stage-other-operation.tmp").lexically_normal();
		const std::filesystem::path other_target =
			(target.parent_path() / L"other-operation.active").lexically_normal();
		const std::filesystem::path other_backup =
			(target.parent_path() /
				L".ashveg-layer-stage-replace-backup-other-operation.tmp").lexically_normal();
		WritePrepareBytes(other_source, fixture.source.source_active_bytes);
		WritePrepareBytes(other_backup, fixture.source.source_active_bytes);
		const AshEngine::VegetationFileInspection other_source_inspection =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				fixture.root.Path(),
				other_source.lexically_relative(fixture.root.Path()));
		const AshEngine::VegetationFileInspection other_backup_inspection =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				fixture.root.Path(),
				other_backup.lexically_relative(fixture.root.Path()));
		REQUIRE(other_source_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(other_source_inspection.file_identity.available);
		REQUIRE(other_backup_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(other_backup_inspection.file_identity.available);
		REQUIRE(fixture.cleanup_registry.TrackStageFile(
			other_source, other_source_inspection.file_identity));
		REQUIRE(fixture.cleanup_registry.BeginStageFilePublish(
			other_source, other_target));
		REQUIRE(fixture.cleanup_registry.RetainStageFileForAtomicReplaceRecovery(
			other_backup, other_source, other_target,
			other_backup_inspection.file_identity));
		REQUIRE(fixture.cleanup_registry.ResolveStageFilePublish(
			other_source,
			AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
		fixture.file_ops.atomic_replace = [other_backup](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = other_backup;
			result.error = "injected other-operation recovery decoy";
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			other_backup, other_source, other_target,
			other_backup_inspection.file_identity));
		CHECK_FALSE(fixture.cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			other_backup, stage, target,
			other_backup_inspection.file_identity));
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		if (fixture.cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		}
		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(other_backup));
		CHECK(fixture.cleanup_registry.CleanupStageFile(
			other_backup, fixture.file_ops));
		CHECK(fixture.cleanup_registry.CleanupStageFile(
			other_source, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit rejects associated stage recovery without its prepared physical identity")
{
	enum class StageFault : uint8_t
	{
		Missing,
		IdentityDrift
	};
	const std::array<StageFault, 2> faults = {
		StageFault::Missing,
		StageFault::IdentityDrift
	};
	for (size_t index = 0; index < faults.size(); ++index)
	{
		CAPTURE(index);
		ExistingCommitFixture fixture(
			"chunk-set-existing-associated-stage-physical-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path stage_relative =
			stage.lexically_relative(fixture.root.Path());
		const AshEngine::VegetationFileIdentity prepared_identity =
			fixture.prepared->stage_file_identity();
		const std::vector<uint8_t> stage_bytes =
			EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256());
		AshEngine::VegetationFileIdentity replacement_identity{};
		fixture.file_ops.atomic_replace = [&, fault = faults[index]](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			if (!registry.BeginStageFilePublish(source, target) ||
				!registry.ResolveStageFilePublish(
					source,
					AshEngine::VegetationStageFilePublishResolution::RecoveryRequired) ||
				!AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
					source, prepared_identity))
			{
				throw std::runtime_error("could not inject associated stage fault");
			}
			if (fault == StageFault::IdentityDrift)
			{
				WritePrepareBytes(source, stage_bytes);
				const AshEngine::VegetationFileInspection replacement =
					AshEngine::get_default_vegetation_file_ops().InspectPath(
						fixture.root.Path(), stage_relative);
				if (replacement.status != AshEngine::VegetationFileResultStatus::Succeeded ||
					!replacement.exists || !replacement.is_regular_file ||
					!replacement.file_identity.available)
				{
					throw std::runtime_error("could not inspect associated stage replacement");
				}
				replacement_identity = replacement.file_identity;
			}
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = source;
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.empty());
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		const AshEngine::VegetationFileBytesResult physical =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		if (faults[index] == StageFault::Missing)
		{
			CHECK(physical.status == AshEngine::VegetationFileResultStatus::NotFound);
		}
		else
		{
			REQUIRE(replacement_identity.available);
			CHECK(replacement_identity.volume_serial_number ==
				prepared_identity.volume_serial_number);
			CHECK(replacement_identity.file_index != prepared_identity.file_index);
			CHECK(physical.status == AshEngine::VegetationFileResultStatus::Succeeded);
			CHECK(physical.bytes == stage_bytes);
		}

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		if (fixture.cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		}
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit validates distinct associated backup ASVA evidence")
{
	enum class BackupFault : uint8_t
	{
		Corrupt,
		WrongSourceDigest
	};
	const std::array<BackupFault, 2> faults = {
		BackupFault::Corrupt,
		BackupFault::WrongSourceDigest
	};
	for (size_t index = 0; index < faults.size(); ++index)
	{
		CAPTURE(index);
		ExistingCommitFixture fixture(
			"chunk-set-existing-associated-backup-evidence-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path target = fixture.ActiveAbsolute();
		const std::filesystem::path backup =
			(target.parent_path() /
				(L".ashveg-layer-stage-replace-backup-invalid-evidence-" +
					std::to_wstring(index) + L".tmp")).lexically_normal();
		std::vector<uint8_t> backup_bytes(48, 0x6d);
		if (faults[index] == BackupFault::WrongSourceDigest)
		{
			AshEngine::VegetationSha256 wrong_manifest{};
			wrong_manifest.fill(0xa7);
			REQUIRE(wrong_manifest != fixture.source.source_manifest_sha256);
			backup_bytes = EncodeActivePointerOrThrow(wrong_manifest);
		}
		WritePrepareBytes(backup, backup_bytes);
		const AshEngine::VegetationFileInspection backup_inspection =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				fixture.root.Path(),
				backup.lexically_relative(fixture.root.Path()));
		REQUIRE(backup_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(backup_inspection.file_identity.available);
		fixture.file_ops.atomic_replace = [backup, backup_inspection](
			const std::filesystem::path& source,
			const std::filesystem::path& publish_target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			if (!registry.BeginStageFilePublish(source, publish_target) ||
				!registry.RetainStageFileForAtomicReplaceRecovery(
					backup, source, publish_target,
					backup_inspection.file_identity) ||
				!registry.ResolveStageFilePublish(
					source,
					AshEngine::VegetationStageFilePublishResolution::TargetPreserved))
			{
				throw std::runtime_error("could not inject associated backup evidence");
			}
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = backup;
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			backup, stage, target, backup_inspection.file_identity));
		const AshEngine::VegetationFileBytesResult retained_backup =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(backup, 48);
		CHECK(retained_backup.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained_backup.bytes == backup_bytes);

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		if (fixture.cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		}
		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(backup));
		CHECK(fixture.cleanup_registry.CleanupStageFile(backup, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit rejects stale or malformed Existing active evidence")
{
	SUBCASE("same 48 bytes under a recreated native identity are SourceChanged")
	{
		ExistingCommitFixture fixture("chunk-set-existing-same-bytes-new-identity");
		AshEngine::IVegetationFileOps& backing =
			AshEngine::get_default_vegetation_file_ops();
		const std::filesystem::path active_relative =
			fixture.source.no_active.active_relative_path;
		const AshEngine::VegetationFileInspection before = backing.InspectPath(
			fixture.root.Path(), active_relative);
		REQUIRE(before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(before.exists);
		REQUIRE(before.is_regular_file);
		REQUIRE(before.file_identity.available);
		const AshEngine::VegetationFileBytesResult before_bytes =
			backing.ReadAllBytes(before.resolved_absolute_path, 48);
		REQUIRE(before_bytes.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(before_bytes.bytes.size() == 48);
		REQUIRE(before_bytes.bytes == fixture.source.source_active_bytes);

		std::error_code remove_error{};
		REQUIRE(std::filesystem::remove(before.resolved_absolute_path, remove_error));
		REQUIRE_FALSE(remove_error);
		WritePrepareBytes(before.resolved_absolute_path, before_bytes.bytes);
		const AshEngine::VegetationFileInspection recreated = backing.InspectPath(
			fixture.root.Path(), active_relative);
		REQUIRE(recreated.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(recreated.exists);
		REQUIRE(recreated.is_regular_file);
		REQUIRE(recreated.file_identity.available);
		const AshEngine::VegetationFileBytesResult recreated_bytes =
			backing.ReadAllBytes(recreated.resolved_absolute_path, 48);
		REQUIRE(recreated_bytes.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(recreated_bytes.bytes == before_bytes.bytes);
		const bool native_identity_changed =
			before.file_identity.volume_serial_number !=
				recreated.file_identity.volume_serial_number ||
			before.file_identity.file_index != recreated.file_identity.file_index;
		REQUIRE(native_identity_changed);

		const std::filesystem::path stage = fixture.prepared->stage_path();
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status ==
			AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult retained =
			backing.ReadAllBytes(recreated.resolved_absolute_path, 48);
		CHECK(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained.bytes == before_bytes.bytes);
		const AshEngine::VegetationFileInspection retained_identity =
			backing.InspectPath(fixture.root.Path(), active_relative);
		CHECK(retained_identity.file_identity.available ==
			recreated.file_identity.available);
		CHECK(retained_identity.file_identity.volume_serial_number ==
			recreated.file_identity.volume_serial_number);
		CHECK(retained_identity.file_identity.file_index ==
			recreated.file_identity.file_index);
		const AshEngine::VegetationFileBytesResult removed_stage =
			backing.ReadAllBytes(stage, 48);
		CHECK(removed_stage.status == AshEngine::VegetationFileResultStatus::NotFound);
		CHECK(removed_stage.bytes.empty());
	}

	SUBCASE("active disappearance is SourceChanged and never falls back to create-new")
	{
		ExistingCommitFixture fixture("chunk-set-existing-missing");
		std::error_code error{};
		REQUIRE(std::filesystem::remove(fixture.ActiveAbsolute(), error));
		REQUIRE_FALSE(error);
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("valid active pointer with another manifest is SourceChanged")
	{
		ExistingCommitFixture fixture("chunk-set-existing-manifest-drift");
		AshEngine::VegetationSha256 other{};
		other.fill(0xa5);
		REQUIRE(other != fixture.source.source_manifest_sha256);
		WritePrepareBytes(fixture.ActiveAbsolute(), EncodeActivePointerOrThrow(other));
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::SourceChanged);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("illegal active inspection payload fails closed")
	{
		ExistingCommitFixture fixture("chunk-set-existing-inspect-illegal");
		fixture.file_ops.inspection_result_hook = [&fixture](
			const std::filesystem::path& path,
			AshEngine::VegetationFileInspection& result)
		{
			if (path == fixture.source.no_active.active_relative_path)
			{
				result.status = AshEngine::VegetationFileResultStatus::Failed;
			}
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("oversized active pointer fails its exact 48-byte bounded read")
	{
		ExistingCommitFixture fixture("chunk-set-existing-active-oversize");
		std::vector<uint8_t> oversized = fixture.source.source_active_bytes;
		oversized.push_back(0);
		WritePrepareBytes(fixture.ActiveAbsolute(), oversized);
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("active pointer CRC corruption fails closed")
	{
		ExistingCommitFixture fixture("chunk-set-existing-active-crc");
		std::vector<uint8_t> corrupted = fixture.source.source_active_bytes;
		corrupted.back() ^= 0x80u;
		WritePrepareBytes(fixture.ActiveAbsolute(), corrupted);
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("illegal active read result shape fails closed")
	{
		ExistingCommitFixture fixture("chunk-set-existing-read-illegal");
		fixture.file_ops.overridden_read_call = 1;
		fixture.file_ops.read_override.status =
			AshEngine::VegetationFileResultStatus::Failed;
		fixture.file_ops.read_override.bytes = fixture.source.source_active_bytes;
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(fixture.file_ops.read_call_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit validates atomic replace recovery evidence without creating ghosts")
{
	SUBCASE("illegal atomic shape falls back only to the still-owned strict stage")
	{
		ExistingCommitFixture fixture("chunk-set-existing-fallback-shape");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [&fixture](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::TargetPreserved;
			result.recovery_path = fixture.ActiveAbsolute();
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.file_ops.read_call_count == 3);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}

	SUBCASE("unprotected RecoveryRequired path falls back to the prepared stage")
	{
		ExistingCommitFixture fixture("chunk-set-existing-fallback-owner");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = source;
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	}

	SUBCASE("corrupt owned stage cannot become fallback recovery")
	{
		ExistingCommitFixture fixture("chunk-set-existing-fallback-corrupt");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationFileBytesResult bytes =
				AshEngine::get_default_vegetation_file_ops().ReadAllBytes(source, 48);
			if (bytes.status != AshEngine::VegetationFileResultStatus::Succeeded)
			{
				throw std::runtime_error("could not inject fallback corruption");
			}
			bytes.bytes.back() ^= 0x40u;
			WritePrepareBytes(source, bytes.bytes);
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.empty());
		CHECK(fixture.file_ops.EventCount("remove-file") == 1);
		const AshEngine::VegetationFileBytesResult removed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed.status == AshEngine::VegetationFileResultStatus::NotFound);
		CHECK(removed.bytes.empty());
	}

	SUBCASE("unknown post-call ownership is never reconstructed from path bytes")
	{
		ExistingCommitFixture fixture("chunk-set-existing-fallback-unknown");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			(void)registry.ForgetConsumedStageFile(source);
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		const AshEngine::VegetationFileBytesResult orphan =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(orphan.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
			stage, fixture.prepared->stage_file_identity()));
	}

	SUBCASE("consumed post-call stage is never recreated as recovery")
	{
		ExistingCommitFixture fixture("chunk-set-existing-fallback-consumed");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const AshEngine::VegetationFileIdentity prepared_identity =
			fixture.prepared->stage_file_identity();
		fixture.file_ops.atomic_replace = [prepared_identity](
			const std::filesystem::path& source,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			(void)AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
				source, prepared_identity);
			(void)registry.ForgetConsumedStageFile(source);
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK_FALSE(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		const AshEngine::VegetationFileBytesResult consumed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(consumed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}
}

TEST_CASE("Vegetation chunk set commit rejects every illegal atomic replace result shape")
{
	enum class IllegalShape : uint8_t
	{
		ReplacedWithPath,
		RecoveryWithoutPath,
		RecoveryWithRelativePath,
		RecoveryWithNonNormalizedPath,
		UnknownStatus
	};
	const std::array<IllegalShape, 5> cases = {
		IllegalShape::ReplacedWithPath,
		IllegalShape::RecoveryWithoutPath,
		IllegalShape::RecoveryWithRelativePath,
		IllegalShape::RecoveryWithNonNormalizedPath,
		IllegalShape::UnknownStatus
	};
	for (size_t index = 0; index < cases.size(); ++index)
	{
		CAPTURE(index);
		ExistingCommitFixture fixture(
			"chunk-set-existing-illegal-atomic-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [&fixture, shape = cases[index], stage](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			switch (shape)
			{
			case IllegalShape::ReplacedWithPath:
				result.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
				result.recovery_path = stage;
				break;
			case IllegalShape::RecoveryWithoutPath:
				result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
				break;
			case IllegalShape::RecoveryWithRelativePath:
				result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
				result.recovery_path = L".ashveg-layer-stage-relative.tmp";
				break;
			case IllegalShape::RecoveryWithNonNormalizedPath:
				result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
				result.recovery_path = fixture.ActiveAbsolute().parent_path() /
					L"nested" / L".." / L".ashveg-layer-stage-nonnormal.tmp";
				break;
			case IllegalShape::UnknownStatus:
				result.status = static_cast<AshEngine::VegetationAtomicReplaceStatus>(0xffu);
				break;
			}
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.read_call_count == 3);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set commit fallback rejects same bytes from a replaced stage identity")
{
	enum class InvalidAtomicReturn : uint8_t
	{
		IllegalShape,
		InvalidRecoveryOwnership
	};
	const std::array<InvalidAtomicReturn, 2> cases = {
		InvalidAtomicReturn::IllegalShape,
		InvalidAtomicReturn::InvalidRecoveryOwnership
	};
	for (size_t index = 0; index < cases.size(); ++index)
	{
		CAPTURE(index);
		ExistingCommitFixture fixture(
			"chunk-set-existing-fallback-native-" + std::to_string(index));
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::filesystem::path stage_relative =
			stage.lexically_relative(fixture.root.Path());
		REQUIRE_FALSE(stage_relative.empty());
		const AshEngine::VegetationFileIdentity prepared_identity =
			fixture.prepared->stage_file_identity();
		REQUIRE(prepared_identity.available);
		const std::vector<uint8_t> exact_stage_bytes =
			EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256());
		AshEngine::VegetationFileIdentity replacement_identity{};
		fixture.file_ops.atomic_replace = [&, invalid_return = cases[index]](
			const std::filesystem::path& source,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			if (!AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
					source, prepared_identity))
			{
				throw std::runtime_error("could not remove stage for identity replacement");
			}
			WritePrepareBytes(source, exact_stage_bytes);
			const AshEngine::VegetationFileInspection replacement =
				AshEngine::get_default_vegetation_file_ops().InspectPath(
					fixture.root.Path(), stage_relative);
			if (replacement.status != AshEngine::VegetationFileResultStatus::Succeeded ||
				!replacement.exists || !replacement.is_regular_file ||
				!replacement.file_identity.available)
			{
				throw std::runtime_error("could not inspect replacement stage identity");
			}
			replacement_identity = replacement.file_identity;
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			if (invalid_return == InvalidAtomicReturn::InvalidRecoveryOwnership)
			{
				result.recovery_path = source;
			}
			return result;
		};

		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		REQUIRE(replacement_identity.available);
		CHECK(replacement_identity.volume_serial_number ==
			prepared_identity.volume_serial_number);
		CHECK(replacement_identity.file_index != prepared_identity.file_index);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK_FALSE(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileInspection survivor =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				fixture.root.Path(), stage_relative);
		REQUIRE(survivor.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(survivor.exists);
		REQUIRE(survivor.is_regular_file);
		REQUIRE(survivor.file_identity.available);
		CHECK(survivor.file_identity.volume_serial_number ==
			replacement_identity.volume_serial_number);
		CHECK(survivor.file_identity.file_index ==
			replacement_identity.file_index);
		const AshEngine::VegetationFileBytesResult retained =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		REQUIRE(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained.bytes == exact_stage_bytes);

		if (fixture.cleanup_registry.IsRecoveryStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		}
		if (fixture.cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		}
		CHECK(AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
			stage, replacement_identity));
	}
}

TEST_CASE("Vegetation chunk set commit Existing path keeps terminal success and contains new exception boundaries")
{
	SUBCASE("late cancellation and bookkeeping cannot downgrade Replaced")
	{
		ExistingCommitFixture fixture("chunk-set-existing-terminal");
		fixture.file_ops.atomic_replace = [&fixture](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			AshEngine::VegetationAtomicReplaceResult result =
				AshEngine::get_default_vegetation_file_ops().AtomicReplace(
					source, target, registry);
			if (result.status == AshEngine::VegetationAtomicReplaceStatus::Replaced)
			{
				fixture.cancellation->store(true, std::memory_order_release);
				(void)registry.ForgetConsumedStageFile(source);
			}
			return result;
		};
		const AshEngine::VegetationChunkSetCommitResult result = fixture.Commit();
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.cancellation->load(std::memory_order_acquire));
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
	}

	SUBCASE("a consumed Existing capability cannot acquire or publish twice")
	{
		ExistingCommitFixture fixture("chunk-set-existing-single-use");
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			return AshEngine::get_default_vegetation_file_ops().AtomicReplace(
				source, target, registry);
		};
		const AshEngine::VegetationChunkSetCommitResult first = fixture.Commit();
		REQUIRE(first.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		const AshEngine::VegetationFileBytesResult winner =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		REQUIRE(winner.status == AshEngine::VegetationFileResultStatus::Succeeded);

		fixture.ResetCommitObservations();
		const AshEngine::VegetationChunkSetCommitResult second = fixture.Commit();
		CHECK(second.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(second.recovery_path.empty());
		CHECK(fixture.file_ops.acquire_call_count == 0);
		CHECK(fixture.file_ops.inspection_call_count == 0);
		CHECK(fixture.file_ops.read_call_count == 0);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult after =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(after.bytes == winner.bytes);
	}

	SUBCASE("active read exception is contained with old active and exact stage cleanup")
	{
		ExistingCommitFixture fixture("chunk-set-existing-throw-read");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.fault_rules.push_back(
			{ "read", 1, PrepareFaultMode::Throw });
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 0);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.bytes == fixture.source.source_active_bytes);
		const AshEngine::VegetationFileBytesResult removed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("AtomicReplace exception is contained with old active and exact stage cleanup")
	{
		ExistingCommitFixture fixture("chunk-set-existing-throw-atomic");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
			-> AshEngine::VegetationAtomicReplaceResult
		{
			throw std::runtime_error("injected atomic replace exception");
		};
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.bytes == fixture.source.source_active_bytes);
		const AshEngine::VegetationFileBytesResult removed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(removed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("exception after default Replaced remains terminal success")
	{
		ExistingCommitFixture fixture("chunk-set-existing-throw-after-replaced");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const std::vector<uint8_t> expected_active =
			EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256());
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
			-> AshEngine::VegetationAtomicReplaceResult
		{
			const AshEngine::VegetationAtomicReplaceResult replaced =
				AshEngine::get_default_vegetation_file_ops().AtomicReplace(
					source, target, registry);
			if (replaced.status != AshEngine::VegetationAtomicReplaceStatus::Replaced)
			{
				throw std::runtime_error("default AtomicReplace did not replace");
			}
			throw std::runtime_error("injected exception after Replaced");
		};
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Succeeded);
		CHECK(result.recovery_path.empty());
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == expected_active);
		const AshEngine::VegetationFileBytesResult consumed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(consumed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("same bytes from a new target identity cannot prove terminal success")
	{
		ExistingCommitFixture fixture("chunk-set-existing-throw-decoy-target");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		const AshEngine::VegetationFileIdentity prepared_identity =
			fixture.prepared->stage_file_identity();
		const std::vector<uint8_t> expected_active =
			EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256());
		AshEngine::VegetationFileIdentity decoy_target_identity{};
		fixture.file_ops.atomic_replace = [&fixture, &expected_active,
			&decoy_target_identity, prepared_identity](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
			-> AshEngine::VegetationAtomicReplaceResult
		{
			AshEngine::IVegetationFileOps& default_ops =
				AshEngine::get_default_vegetation_file_ops();
			if (!default_ops.RemoveOwnedStageFile(source, prepared_identity))
			{
				throw std::runtime_error("could not remove staged source for decoy target");
			}
			std::error_code remove_target_error{};
			if (!std::filesystem::remove(target, remove_target_error) ||
				remove_target_error)
			{
				throw std::runtime_error("could not remove active target for decoy target");
			}
			WritePrepareBytes(target, expected_active);
			const AshEngine::VegetationFileInspection decoy = default_ops.InspectPath(
				fixture.root.Path(), fixture.source.no_active.active_relative_path);
			if (decoy.status != AshEngine::VegetationFileResultStatus::Succeeded ||
				!decoy.exists || !decoy.is_regular_file ||
				!decoy.file_identity.available ||
				!registry.ForgetConsumedStageFile(source))
			{
				throw std::runtime_error("could not establish decoy target identity");
			}
			decoy_target_identity = decoy.file_identity;
			throw std::runtime_error("injected exception after decoy publication");
		};

		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		REQUIRE(decoy_target_identity.available);
		CHECK(decoy_target_identity.volume_serial_number ==
			prepared_identity.volume_serial_number);
		CHECK(decoy_target_identity.file_index != prepared_identity.file_index);
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
		CHECK(result.recovery_path.empty());
		CHECK(fixture.cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult target_bytes =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(target_bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(target_bytes.bytes == expected_active);
		const AshEngine::VegetationFileBytesResult consumed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(consumed.status == AshEngine::VegetationFileResultStatus::NotFound);
	}

	SUBCASE("exception after publication pin preserves exact Publishing recovery")
	{
		ExistingCommitFixture fixture("chunk-set-existing-throw-publishing");
		const std::filesystem::path stage = fixture.prepared->stage_path();
		fixture.file_ops.atomic_replace = [](
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
			-> AshEngine::VegetationAtomicReplaceResult
		{
			if (!registry.BeginStageFilePublish(source, target))
			{
				throw std::runtime_error("could not inject publishing pin");
			}
			throw std::runtime_error("injected exception after publishing pin");
		};
		AshEngine::VegetationChunkSetCommitResult result{};
		CHECK_NOTHROW(result = fixture.Commit());
		CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::RecoveryRequired);
		CHECK(result.recovery_path == stage);
		CHECK(*fixture.file_ops.lease_destruction_count == 1);
		CHECK(fixture.file_ops.atomic_replace_call_count == 1);
		CHECK(fixture.file_ops.create_new_call_count == 0);
		CHECK(fixture.file_ops.EventCount("remove-file") == 0);
		CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
		CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
		const AshEngine::VegetationFileBytesResult retained =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(stage, 48);
		CHECK(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				fixture.ActiveAbsolute(), 48);
		CHECK(active.bytes == fixture.source.source_active_bytes);

		CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
		CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
		CHECK(fixture.cleanup_registry.empty());
	}
	}

TEST_CASE("Vegetation chunk set commit keeps an unresolved Existing publish pin when every exception probe throws")
{
	bool reject_inspections = false;
	bool reject_reads = false;
	SUBCASE("inspection probes keep throwing")
	{
		reject_inspections = true;
	}
	SUBCASE("inspection succeeds and byte probes keep throwing")
	{
		reject_reads = true;
	}
	REQUIRE(reject_inspections != reject_reads);
	ExistingCommitFixture fixture("chunk-set-existing-throw-publishing-probes");
	const std::filesystem::path stage = fixture.prepared->stage_path();
	const std::filesystem::path target = fixture.ActiveAbsolute();
	bool publishing_begun = false;
	size_t rejected_inspections = 0;
	size_t rejected_reads = 0;
	fixture.file_ops.inspection_result_hook =
		[&publishing_begun, &rejected_inspections, reject_inspections](
			const std::filesystem::path&,
			AshEngine::VegetationFileInspection&)
		{
			if (publishing_begun && reject_inspections)
			{
				++rejected_inspections;
				throw std::runtime_error(
					"injected persistent post-publish inspection failure");
			}
		};
	fixture.file_ops.read_result_hook =
		[&publishing_begun, &rejected_reads, reject_reads](
			const std::filesystem::path&,
			const uint64_t,
			AshEngine::VegetationFileBytesResult&)
		{
			if (publishing_begun && reject_reads)
			{
				++rejected_reads;
				throw std::runtime_error(
					"injected persistent post-publish byte failure");
			}
		};
	fixture.file_ops.atomic_replace = [&publishing_begun](
		const std::filesystem::path& source,
		const std::filesystem::path& publish_target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		-> AshEngine::VegetationAtomicReplaceResult
	{
		if (!registry.BeginStageFilePublish(source, publish_target))
		{
			throw std::runtime_error("could not inject unresolved publishing pin");
		}
		publishing_begun = true;
		throw std::runtime_error("injected exception after unresolved publishing pin");
	};

	AshEngine::VegetationChunkSetCommitResult result{};
	CHECK_NOTHROW(result = fixture.Commit());
	CHECK(publishing_begun);
	if (reject_inspections)
	{
		CHECK(rejected_inspections >= 2);
		CHECK(rejected_reads == 0);
	}
	else
	{
		CHECK(rejected_inspections == 0);
		CHECK(rejected_reads >= 1);
	}
	CHECK(result.status == AshEngine::VegetationChunkSetCommitStatus::Failed);
	CHECK(result.recovery_path.empty());
	CHECK_FALSE(result.error.empty());
	CHECK(*fixture.file_ops.lease_destruction_count == 1);
	CHECK(fixture.file_ops.atomic_replace_call_count == 1);
	CHECK(fixture.file_ops.create_new_call_count == 0);
	CHECK(fixture.file_ops.EventCount("remove-file") == 0);
	CHECK(fixture.cleanup_registry.OwnsStageFile(stage));
	CHECK(fixture.cleanup_registry.IsRecoveryStageFile(stage));
	CHECK(fixture.cleanup_registry.IsAtomicReplaceRecoveryStageFile(
		stage, stage, target, fixture.prepared->stage_file_identity()));

	AshEngine::IVegetationFileOps& backing =
		AshEngine::get_default_vegetation_file_ops();
	const AshEngine::VegetationFileBytesResult active =
		backing.ReadAllBytes(target, 48);
	REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(active.bytes.size() == 48);
	CHECK(active.bytes == fixture.source.source_active_bytes);
	const AshEngine::VegetationFileBytesResult retained =
		backing.ReadAllBytes(stage, 48);
	REQUIRE(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(retained.bytes.size() == 48);
	CHECK(retained.bytes ==
		EncodeActivePointerOrThrow(fixture.prepared->manifest_sha256()));

	CHECK(fixture.cleanup_registry.ReleaseRecoveryStageFile(stage));
	CHECK(fixture.cleanup_registry.CleanupStageFile(stage, fixture.file_ops));
	CHECK(fixture.cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare stages one no-active transaction without publishing active")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-no-active");
	const std::filesystem::path layer_relative_path =
		"vegetation/meadow.AshVegetationLayer";
	std::filesystem::path store_relative_path = layer_relative_path;
	store_relative_path += ".AshVegetationChunks";
	const std::filesystem::path objects_relative_path =
		store_relative_path / "objects";
	const std::filesystem::path manifests_relative_path =
		store_relative_path / "manifests";
	const std::filesystem::path staging_relative_path =
		store_relative_path / "staging";
	const std::filesystem::path active_relative_path =
		store_relative_path / "active.asva";

	const AshEngine::VegetationBakeInput input = SingleChunkBakeInput(0x1234u, 7);
	root.Write(layer_relative_path, EncodeLayerOrThrow(*input.layer_snapshot));
	const AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	const AshEngine::VegetationBakeTransactionOutput& transaction =
		RequireTransaction(baked);
	REQUIRE(transaction.chunks.size() == 1);

	std::vector<uint8_t> manifest_bytes{};
	std::string encode_error{};
	REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
		transaction.resulting_manifest, manifest_bytes, &encode_error));
	const AshEngine::VegetationSha256 manifest_sha256 = AshEngine::vegetation_sha256(
		manifest_bytes.data(), manifest_bytes.size());
	const std::filesystem::path object_relative_path = objects_relative_path /
		(VegetationTest::ToHex(transaction.chunks[0].object_sha256) +
			".AshVegetationChunk");
	const std::filesystem::path manifest_relative_path = manifests_relative_path /
		(VegetationTest::ToHex(manifest_sha256) + ".asvm");
	AshEngine::VegetationChunkSetActivePointer pointer{};
	pointer.manifest_sha256 = manifest_sha256;
	std::vector<uint8_t> active_bytes{};
	REQUIRE(AshEngine::encode_vegetation_chunk_set_active_pointer(
		pointer, active_bytes, &encode_error));
	REQUIRE(active_bytes.size() == 48);

	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), layer_relative_path, baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	REQUIRE(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);

	const std::filesystem::path absolute_root =
		std::filesystem::absolute(root.Path()).lexically_normal();
	const std::filesystem::path object_absolute_path =
		(absolute_root / object_relative_path).lexically_normal();
	const std::filesystem::path manifest_absolute_path =
		(absolute_root / manifest_relative_path).lexically_normal();
	const std::filesystem::path active_absolute_path =
		(absolute_root / active_relative_path).lexically_normal();
	CHECK(prepared.asset_root() == absolute_root);
	CHECK(prepared.layer_canonical_relative_path() == layer_relative_path);
	CHECK(prepared.layer_resolved_absolute_path() ==
		(absolute_root / layer_relative_path).lexically_normal());
	CHECK_FALSE(prepared.layer_canonical_identity().empty());
	CHECK(prepared.store_canonical_relative_path() == store_relative_path);
	CHECK(prepared.store_resolved_absolute_path() ==
		(absolute_root / store_relative_path).lexically_normal());
	CHECK_FALSE(prepared.store_canonical_identity().empty());
	CHECK(prepared.active_canonical_relative_path() == active_relative_path);
	CHECK(prepared.active_resolved_absolute_path() == active_absolute_path);
	CHECK_FALSE(prepared.active_canonical_identity().empty());
	CHECK(prepared.source_active_identity() == transaction.source_active_identity);
	CHECK(prepared.expected_identity() == transaction.expected_identity);
	CHECK(prepared.manifest_sha256() == manifest_sha256);
	CHECK(prepared.stage_path() == file_ops.sibling_stage);
	CHECK(prepared.stage_file_identity().available);
	CHECK(prepared.stage_file_identity().volume_serial_number ==
		file_ops.sibling_identity.volume_serial_number);
	CHECK(prepared.stage_file_identity().file_index ==
		file_ops.sibling_identity.file_index);
	CHECK(prepared.active_stage_size() == active_bytes.size());
	CHECK(prepared.active_stage_sha256() == AshEngine::vegetation_sha256(
		active_bytes.data(), active_bytes.size()));
	CHECK(prepared.error().empty());

	REQUIRE(file_ops.child_stage_paths.size() == 2);
	CHECK(file_ops.stage_tree.parent_path() ==
		(absolute_root / staging_relative_path).lexically_normal());
	CHECK_FALSE(cleanup_registry.OwnsStageTree(file_ops.stage_tree));
	CHECK_FALSE(cleanup_registry.OwnsStageFile(file_ops.child_stage_paths[0]));
	CHECK_FALSE(cleanup_registry.OwnsStageFile(file_ops.child_stage_paths[1]));
	CHECK(cleanup_registry.OwnsStageFile(prepared.stage_path()));
	CHECK_FALSE(cleanup_registry.empty());
	const AshEngine::VegetationFileBytesResult object_on_disk =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			object_absolute_path, transaction.chunks[0].object_bytes.size());
	const AshEngine::VegetationFileBytesResult manifest_on_disk =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			manifest_absolute_path, manifest_bytes.size());
	const AshEngine::VegetationFileBytesResult active_stage_on_disk =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			prepared.stage_path(), active_bytes.size());
	REQUIRE(object_on_disk.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(manifest_on_disk.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(active_stage_on_disk.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(object_on_disk.bytes == transaction.chunks[0].object_bytes);
	CHECK(manifest_on_disk.bytes == manifest_bytes);
	CHECK(active_stage_on_disk.bytes == active_bytes);
	std::error_code exists_error{};
	CHECK_FALSE(std::filesystem::exists(active_absolute_path, exists_error));
	CHECK_FALSE(exists_error);

	const std::vector<std::string> expected_event_names = {
		"inspect",
		"ensure", "ensure", "ensure", "ensure",
		"inspect", "create-tree",
		"create-child", "object.write", "object.flush", "read", "publish",
		"inspect", "read",
		"create-child", "manifest.write", "manifest.flush", "read", "publish",
		"inspect", "read", "remove-tree",
		"inspect", "create-sibling", "inspect",
		"active.write", "active.flush", "read"
	};
	CHECK(file_ops.EventNames() == expected_event_names);
	REQUIRE(file_ops.events.size() == expected_event_names.size());
	CHECK(file_ops.events[0].path == layer_relative_path);
	CHECK(file_ops.events[1].path == store_relative_path);
	CHECK(file_ops.events[2].path == objects_relative_path);
	CHECK(file_ops.events[3].path == manifests_relative_path);
	CHECK(file_ops.events[4].path == staging_relative_path);
	CHECK(file_ops.events[5].path == store_relative_path);
	CHECK(file_ops.events[6].path ==
		(absolute_root / staging_relative_path).lexically_normal());
	CHECK(file_ops.events[6].value == transaction.expected_identity.operation_serial);
	CHECK(file_ops.events[7].auxiliary_path == file_ops.stage_tree);
	CHECK(file_ops.events[8].value >= 1);
	CHECK(file_ops.events[8].value <= 1024ull * 1024ull);
	CHECK(file_ops.events[11].path == object_absolute_path);
	CHECK(file_ops.events[14].auxiliary_path == file_ops.stage_tree);
	CHECK(file_ops.events[15].value >= 1);
	CHECK(file_ops.events[15].value <= 1024ull * 1024ull);
	CHECK(file_ops.events[18].path == manifest_absolute_path);
	CHECK(file_ops.events[21].path == file_ops.stage_tree);
	CHECK(file_ops.events[22].path == active_relative_path);
	CHECK(file_ops.events[23].path == active_absolute_path);
	CHECK(file_ops.events[23].value == transaction.expected_identity.operation_serial);
	CHECK(file_ops.events[24].path == active_relative_path);
	CHECK(file_ops.events[25].value == 48);

	const std::filesystem::path active_stage_path = prepared.stage_path();
	AshEngine::VegetationPreparedChunkSet moved(std::move(prepared));
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	CHECK(prepared.stage_path().empty());
	CHECK(prepared.active_stage_size() == 0);
	CHECK(prepared.active_stage_sha256() == AshEngine::VegetationSha256{});
	CHECK(moved.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
	CHECK(moved.stage_path() == active_stage_path);

	CHECK(cleanup_registry.CleanupStageFile(active_stage_path, file_ops));
	CHECK(cleanup_registry.empty());
	CHECK_FALSE(std::filesystem::exists(active_stage_path, exists_error));
	CHECK_FALSE(exists_error);
}

TEST_CASE("Vegetation chunk set prepare binds one absolute root across a caller CWD change")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-bound-root");
	const NoActivePrepareFixture fixture{};
	const std::filesystem::path relative_asset_root = "bound-assets";
	const std::filesystem::path decoy_cwd = root.Path() / "decoy-cwd";
	root.Write(relative_asset_root / fixture.layer_relative_path,
		EncodeLayerOrThrow(*fixture.input.layer_snapshot));
	REQUIRE(AshEngine::get_default_vegetation_file_ops().EnsureDirectoryTree(
		root.Path(), "decoy-cwd") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(AshEngine::get_default_vegetation_file_ops().EnsureDirectoryTree(
		decoy_cwd, relative_asset_root) ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path bound_asset_root =
		std::filesystem::absolute(
			root.Path() / relative_asset_root).lexically_normal();

	ScopedCurrentPathRestore cwd_restore{};
	cwd_restore.Set(root.Path());
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.change_cwd_after_inspection_call = 1;
	file_ops.changed_cwd = decoy_cwd;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			relative_asset_root, fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
	CHECK(prepared.asset_root() == bound_asset_root);
	REQUIRE_FALSE(file_ops.root_arguments.empty());
	for (const std::filesystem::path& supplied_root : file_ops.root_arguments)
	{
		CHECK(supplied_root == bound_asset_root);
		CHECK(supplied_root.is_absolute());
		CHECK(supplied_root.lexically_normal() == supplied_root);
	}

	const AshEngine::VegetationFileInspection decoy_store =
		AshEngine::get_default_vegetation_file_ops().InspectPath(
			decoy_cwd, relative_asset_root / fixture.store_relative_path);
	REQUIRE(decoy_store.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK_FALSE(decoy_store.exists);
	CHECK_FALSE(decoy_store.is_regular_file);
	if (!prepared.stage_path().empty() &&
		cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
	}
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare merges an existing source without republishing untouched objects")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-merge");
	const ExistingPrepareFixture fixture{};
	fixture.Write(root);
	std::vector<uint8_t> resulting_manifest_bytes{};
	std::string error{};
	REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
		fixture.Transaction().resulting_manifest,
		resulting_manifest_bytes, &error));
	const AshEngine::VegetationSha256 resulting_manifest_sha256 =
		AshEngine::vegetation_sha256(
			resulting_manifest_bytes.data(), resulting_manifest_bytes.size());
	const std::filesystem::path absolute_root =
		std::filesystem::absolute(root.Path()).lexically_normal();
	const std::filesystem::path active_absolute =
		(absolute_root / fixture.no_active.active_relative_path).lexically_normal();

	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	REQUIRE(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
	CHECK(prepared.source_active_identity() ==
		fixture.Transaction().source_active_identity);
	CHECK(prepared.manifest_sha256() == resulting_manifest_sha256);
	CHECK(file_ops.publish_results.size() ==
		fixture.Transaction().chunks.size() + 1);
	CHECK(file_ops.EventCount("publish") == fixture.Transaction().chunks.size() + 1);
	CHECK(file_ops.child_stage_paths.size() ==
		fixture.Transaction().chunks.size() + 1);
	for (const AshEngine::VegetationBakedChunk& chunk : fixture.Transaction().chunks)
	{
		const std::filesystem::path expected_object =
			(absolute_root / fixture.no_active.store_relative_path / "objects" /
				(VegetationTest::ToHex(chunk.object_sha256) +
					".AshVegetationChunk")).lexically_normal();
		CHECK(std::count_if(file_ops.events.begin(), file_ops.events.end(),
			[&](const PrepareFileOpEvent& event)
			{
				return event.name == "publish" && event.path == expected_object;
			}) == 1);
	}
	const std::filesystem::path expected_manifest =
		(absolute_root / fixture.no_active.store_relative_path / "manifests" /
			(VegetationTest::ToHex(resulting_manifest_sha256) + ".asvm"))
			.lexically_normal();
	CHECK(std::count_if(file_ops.events.begin(), file_ops.events.end(),
		[&](const PrepareFileOpEvent& event)
		{
			return event.name == "publish" && event.path == expected_manifest;
		}) == 1);

	const std::filesystem::path source_manifest_absolute =
		(absolute_root / fixture.source_manifest_relative_path).lexically_normal();
	const uint64_t expected_source_ceiling = 96ull + 80ull *
		(fixture.Transaction().resulting_manifest.entries.size() +
			fixture.Transaction().expected_identity.target_coords.size());
	CHECK(std::count_if(file_ops.events.begin(), file_ops.events.end(),
		[&](const PrepareFileOpEvent& event)
		{
			return event.name == "read" &&
				event.path == source_manifest_absolute &&
				event.value == expected_source_ceiling;
		}) == 1);
	const AshEngine::VegetationFileBytesResult active_after =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			active_absolute, fixture.source_active_bytes.size());
	REQUIRE(active_after.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(active_after.bytes == fixture.source_active_bytes);
	CHECK(prepared.stage_path() == file_ops.sibling_stage);
	CHECK(prepared.stage_file_identity().available);
	CHECK(cleanup_registry.OwnsStageFile(prepared.stage_path()));
	CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare rejects every non-target resulting-manifest mutation")
{
	auto check_rejected = [](const std::string& slug, auto&& mutate)
	{
		VegetationTest::ScopedAssetRoot root(slug);
		ExistingPrepareFixture fixture{};
		mutate(*fixture.baked.transaction);
		fixture.Write(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	};

	SUBCASE("modified untouched entry")
	{
		check_rejected("chunk-set-prepare-existing-modified-untouched",
			[](AshEngine::VegetationBakeTransactionOutput& transaction)
			{
				++transaction.resulting_manifest.entries.back().object_sha256[0];
			});
	}

	SUBCASE("missing untouched entry")
	{
		check_rejected("chunk-set-prepare-existing-missing-untouched",
			[](AshEngine::VegetationBakeTransactionOutput& transaction)
			{
				transaction.resulting_manifest.entries.pop_back();
			});
	}

	SUBCASE("injected non-target entry")
	{
		check_rejected("chunk-set-prepare-existing-injected-untouched",
			[](AshEngine::VegetationBakeTransactionOutput& transaction)
			{
				AshEngine::VegetationChunkSetManifestEntry injected{};
				injected.coord = { 4, 0 };
				injected.object_sha256.fill(0x74);
				injected.input_sha256.fill(0x84);
				transaction.resulting_manifest.entries.push_back(injected);
			});
	}
}

TEST_CASE("Vegetation chunk set prepare strictly reads one bounded existing source manifest")
{
	auto expected_ceiling = [](const ExistingPrepareFixture& fixture)
	{
		return 96ull + 80ull *
			(fixture.Transaction().resulting_manifest.entries.size() +
				fixture.Transaction().expected_identity.target_coords.size());
	};
	auto check_no_mutation = [](const RecordingImmutablePublishFileOps& file_ops,
		const AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry)
	{
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	};

	SUBCASE("missing source manifest")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-missing");
		const ExistingPrepareFixture fixture{};
		fixture.WriteLayer(root);
		fixture.no_active.WriteAsset(root, fixture.no_active.active_relative_path,
			fixture.source_active_bytes);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("inspect") == 2);
		CHECK(file_ops.EventCount("read") == 0);
		check_no_mutation(file_ops, cleanup_registry);
	}

	SUBCASE("bounded read failure")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-read-fail");
		const ExistingPrepareFixture fixture{};
		fixture.Write(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.overridden_read_call = 1;
		file_ops.read_override.status = AshEngine::VegetationFileResultStatus::Failed;
		file_ops.read_override.error = "injected source read failure";
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.EventCount("read") == 1);
		const auto read = std::find_if(file_ops.events.begin(), file_ops.events.end(),
			[](const PrepareFileOpEvent& event) { return event.name == "read"; });
		REQUIRE(read != file_ops.events.end());
		CHECK(read->value == expected_ceiling(fixture));
		check_no_mutation(file_ops, cleanup_registry);
	}

	SUBCASE("source digest path does not match its bytes")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-digest");
		const ExistingPrepareFixture fixture{};
		fixture.WriteLayer(root);
		std::vector<uint8_t> mismatched = fixture.source_manifest_bytes;
		REQUIRE_FALSE(mismatched.empty());
		mismatched.back() ^= 0x01u;
		fixture.no_active.WriteAsset(root, fixture.source_manifest_relative_path,
			mismatched);
		fixture.no_active.WriteAsset(root, fixture.no_active.active_relative_path,
			fixture.source_active_bytes);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		check_no_mutation(file_ops, cleanup_registry);
	}

	SUBCASE("noncanonical source codec bytes with a matching path digest")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-codec");
		ExistingPrepareFixture fixture{};
		std::vector<uint8_t> noncanonical = fixture.source_manifest_bytes;
		REQUIRE(noncanonical.size() == 96 + 80 * 3);
		std::swap_ranges(noncanonical.begin() + 96,
			noncanonical.begin() + 176, noncanonical.begin() + 176);
		RepairAsvmPayloadAndHeaderCrc(noncanonical);
		fixture.BindSourceBytes(std::move(noncanonical));
		fixture.Write(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		check_no_mutation(file_ops, cleanup_registry);
	}
}

TEST_CASE("Vegetation chunk set prepare applies existing source identity rules only to untouched entries")
{
	auto prepare = [](VegetationTest::ScopedAssetRoot& root,
		ExistingPrepareFixture& fixture,
		RecordingImmutablePublishFileOps& file_ops,
		AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry)
	{
		fixture.Write(root);
		return AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	};

	SUBCASE("generation-only source change permits an exact untouched entry")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-generation");
		ExistingPrepareFixture fixture{};
		--fixture.source_manifest.layer_generation;
		fixture.RefreshSourceIdentity();
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			prepare(root, fixture, file_ops, cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("layer identity change rejects an untouched entry")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-layer-change");
		ExistingPrepareFixture fixture{};
		++fixture.source_manifest.layer_id[0];
		fixture.RefreshSourceIdentity();
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			prepare(root, fixture, file_ops, cleanup_registry);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("surface identity change rejects an untouched entry")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-surface-change");
		ExistingPrepareFixture fixture{};
		++fixture.source_manifest.surface_identity.content_revision;
		fixture.RefreshSourceIdentity();
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			prepare(root, fixture, file_ops, cleanup_registry);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("full rebake permits identity change when every source coordinate is targeted")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-full-rebake");
		ExistingPrepareFixture fixture{};
		++fixture.source_manifest.layer_id[0];
		++fixture.source_manifest.surface_identity.content_revision;
		fixture.RefreshSourceIdentity();
		auto& transaction = *fixture.baked.transaction;
		transaction.full_rebake_required = true;
		transaction.removed_coords.push_back({ 3, 0 });
		transaction.expected_identity.target_coords.push_back({ 3, 0 });
		transaction.resulting_manifest.entries.pop_back();
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			prepare(root, fixture, file_ops, cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("identity change with every source coordinate targeted still requires full rebake")
	{
		VegetationTest::ScopedAssetRoot root(
			"chunk-set-prepare-existing-full-rebake-required");
		ExistingPrepareFixture fixture{};
		++fixture.source_manifest.layer_id[0];
		++fixture.source_manifest.surface_identity.content_revision;
		fixture.RefreshSourceIdentity();
		auto& transaction = *fixture.baked.transaction;
		transaction.removed_coords.push_back({ 3, 0 });
		transaction.expected_identity.target_coords.push_back({ 3, 0 });
		transaction.resulting_manifest.entries.pop_back();
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			prepare(root, fixture, file_ops, cleanup_registry);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("read") == 1);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare bounds and controls existing source verification")
{
	SUBCASE("source entry count above result plus target ceiling fails without mutation")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-ceiling");
		ExistingPrepareFixture fixture{};
		for (int64_t x = 4; x <= 7; ++x)
		{
			AshEngine::VegetationChunkSetManifestEntry entry{};
			entry.coord = { x, 0 };
			entry.object_sha256.fill(static_cast<uint8_t>(0x70 + x));
			entry.input_sha256.fill(static_cast<uint8_t>(0x80 + x));
			fixture.source_manifest.entries.push_back(entry);
		}
		fixture.RefreshSourceIdentity();
		fixture.Write(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.EventCount("read") == 1);
		const auto read = std::find_if(file_ops.events.begin(), file_ops.events.end(),
			[](const PrepareFileOpEvent& event) { return event.name == "read"; });
		REQUIRE(read != file_ops.events.end());
		CHECK(read->value == 96ull + 80ull * 6ull);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("cancellation after source inspection stops before its read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-cancel");
		const ExistingPrepareFixture fixture{};
		fixture.Write(root);
		auto cancellation = std::make_shared<std::atomic_bool>(false);
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = cancellation;
		control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancellation = cancellation;
		file_ops.cancel_after_inspection_call = 2;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				control, cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Cancelled);
		CHECK(file_ops.EventCount("read") == 0);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("cancellation after the single source read stops before mutation")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-read-cancel");
		const ExistingPrepareFixture fixture{};
		fixture.Write(root);
		auto cancellation = std::make_shared<std::atomic_bool>(false);
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = cancellation;
		control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancellation = cancellation;
		file_ops.cancel_after_read_call = 1;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				control, cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Cancelled);
		CHECK(file_ops.EventCount("read") == 1);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("expired deadline stops before source verification or mutation")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-source-timeout");
		const ExistingPrepareFixture fixture{};
		fixture.Write(root);
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::seconds(2));
		control.deadline = std::chrono::steady_clock::now() -
			std::chrono::milliseconds(1);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				control, cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::TimedOut);
		CHECK_FALSE(file_ops.HasMutationEvent());
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare requires one stable existing active identity around sibling creation")
{
	SUBCASE("absent existing active target fails without creating a sibling")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-active-absent");
		const ExistingPrepareFixture fixture{};
		fixture.WriteLayer(root);
		fixture.no_active.WriteAsset(root, fixture.source_manifest_relative_path,
			fixture.source_manifest_bytes);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.sibling_stage.empty());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("non-regular existing active target fails without creating a sibling")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-active-directory");
		const ExistingPrepareFixture fixture{};
		fixture.WriteLayer(root);
		fixture.no_active.WriteAsset(root, fixture.source_manifest_relative_path,
			fixture.source_manifest_bytes);
		REQUIRE(AshEngine::get_default_vegetation_file_ops().EnsureDirectoryTree(
			root.Path(), fixture.no_active.active_relative_path) ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.sibling_stage.empty());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("identity switch after sibling creation preserves active bytes and cleans only that sibling")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-active-switch");
		const ExistingPrepareFixture fixture{};
		fixture.Write(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.rewrite_identity_inspection_path =
			fixture.no_active.active_relative_path;
		file_ops.rewrite_identity_path_occurrence = 2;
		file_ops.rewritten_file_identity.available = true;
		file_ops.rewritten_file_identity.volume_serial_number = 0x1234;
		file_ops.rewritten_file_identity.file_index = 0x5678;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.no_active.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE_FALSE(file_ops.sibling_stage.empty());
		CHECK(file_ops.EventCount("remove-file") == 1);
		const auto removed_stage = std::find_if(
			file_ops.events.begin(), file_ops.events.end(),
			[](const PrepareFileOpEvent& event)
			{
				return event.name == "remove-file";
			});
		REQUIRE(removed_stage != file_ops.events.end());
		CHECK(removed_stage->path == file_ops.sibling_stage);
		std::error_code sibling_exists_error{};
		CHECK_FALSE(std::filesystem::exists(
			file_ops.sibling_stage, sibling_exists_error));
		CHECK_FALSE(sibling_exists_error);
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.no_active.active_relative_path).lexically_normal(),
				fixture.source_active_bytes.size());
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(active.bytes == fixture.source_active_bytes);
	}
}

TEST_CASE("Vegetation chunk set prepare verifies immutable AlreadyExists targets without overwriting")
{
	SUBCASE("an exact existing ASVC is accepted and the manifest still prepares")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-exact");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		fixture.WriteAsset(root, fixture.object_relative_path,
			fixture.Transaction().chunks[0].object_bytes);

		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		REQUIRE(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		REQUIRE(file_ops.publish_results.size() == 2);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK(file_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK_FALSE(cleanup_registry.OwnsStageTree(file_ops.stage_tree));
		CHECK(cleanup_registry.OwnsStageFile(prepared.stage_path()));
		const AshEngine::VegetationFileBytesResult object_bytes =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.object_relative_path).lexically_normal(),
				fixture.Transaction().chunks[0].object_bytes.size());
		REQUIRE(object_bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(object_bytes.bytes == fixture.Transaction().chunks[0].object_bytes);
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("an exact existing ASVM is accepted without overwriting")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-manifest-exact");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		fixture.WriteAsset(root, fixture.manifest_relative_path,
			fixture.manifest_bytes);

		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		REQUIRE(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		REQUIRE(file_ops.publish_results.size() == 2);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		const AshEngine::VegetationFileBytesResult preserved =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.manifest_relative_path).lexically_normal(),
				fixture.manifest_bytes.size());
		REQUIRE(preserved.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.bytes == fixture.manifest_bytes);
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("a one-byte different ASVM is rejected and preserved")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-manifest-mismatch");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		std::vector<uint8_t> different = fixture.manifest_bytes;
		REQUIRE_FALSE(different.empty());
		different.back() ^= 0x01u;
		fixture.WriteAsset(root, fixture.manifest_relative_path, different);

		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.publish_results.size() == 2);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult preserved =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.manifest_relative_path).lexically_normal(),
				different.size());
		REQUIRE(preserved.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.bytes == different);
	}

	SUBCASE("a one-byte different ASVC is rejected and preserved")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-byte-mismatch");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		std::vector<uint8_t> different = fixture.Transaction().chunks[0].object_bytes;
		REQUIRE_FALSE(different.empty());
		different.back() ^= 0x01u;
		fixture.WriteAsset(root, fixture.object_relative_path, different);

		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.publish_results.size() == 1);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK_FALSE(file_ops.HasEvent("manifest.write"));
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult preserved =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.object_relative_path).lexically_normal(),
				different.size());
		REQUIRE(preserved.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.bytes == different);
	}

	SUBCASE("a different valid ASVC is rejected and preserved")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-existing-valid-mismatch");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::VegetationChunk different_chunk =
			fixture.Transaction().chunks[0].chunk;
		different_chunk.chunk_input_sha256[0] ^= 0x01u;
		std::vector<uint8_t> different{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk(
			different_chunk, different, &error));
		REQUIRE(different != fixture.Transaction().chunks[0].object_bytes);
		fixture.WriteAsset(root, fixture.object_relative_path, different);

		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.publish_results.size() == 1);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK(file_ops.EventCount("publish") == 1);
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult preserved =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / fixture.object_relative_path).lexically_normal(),
				different.size());
		REQUIRE(preserved.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.bytes == different);
	}
}

TEST_CASE("Vegetation chunk set prepare reverifies a newly-created immutable target")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-created-corruption");
	const NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.corrupt_after_created_publish_call = 1;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	REQUIRE(file_ops.publish_results.size() == 1);
	CHECK(file_ops.publish_results[0] == AshEngine::VegetationCreateNewStatus::Created);
	CHECK_FALSE(file_ops.HasEvent("manifest.write"));
	CHECK_FALSE(file_ops.HasEvent("create-sibling"));
	CHECK(cleanup_registry.empty());
	const AshEngine::VegetationFileBytesResult corrupted =
		AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
			(root.Path() / fixture.object_relative_path).lexically_normal(),
			fixture.Transaction().chunks[0].object_bytes.size());
	REQUIRE(corrupted.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(corrupted.bytes != fixture.Transaction().chunks[0].object_bytes);
}

TEST_CASE("Vegetation chunk set prepare retains the exact tree when manifest failure cleanup fails")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-manifest-recovery");
	const NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.fail_publish_call = 2;
	file_ops.fail_remove_tree = true;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() ==
		AshEngine::VegetationChunkSetPrepareStatus::RecoveryRequired);
	CHECK(cleanup_registry.OwnsStageTree(file_ops.stage_tree));
	CHECK_FALSE(cleanup_registry.empty());
	CHECK(file_ops.sibling_stage.empty());
	for (const std::filesystem::path& child : file_ops.child_stage_paths)
	{
		CHECK_FALSE(cleanup_registry.OwnsStageFile(child));
	}
	file_ops.fail_remove_tree = false;
	CHECK(cleanup_registry.CleanupStageTree(file_ops.stage_tree, file_ops));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare never deletes an unclaimed stage tree")
{
	SUBCASE("ownership collision preserves the tree already registered by another operation")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-tree-collision");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::IVegetationFileOps& backing =
			AshEngine::get_default_vegetation_file_ops();
		const std::filesystem::path absolute_root =
			std::filesystem::absolute(root.Path()).lexically_normal();
		const std::filesystem::path staging_relative =
			fixture.store_relative_path / "staging";
		const std::filesystem::path staging_absolute =
			(absolute_root / staging_relative).lexically_normal();
		REQUIRE(backing.EnsureDirectoryTree(root.Path(), staging_relative) ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		const AshEngine::VegetationStageTreeResult existing =
			backing.CreateUniqueStageTree(
				staging_absolute,
				fixture.Transaction().expected_identity.operation_serial + 1000);
		REQUIRE(existing.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE_FALSE(existing.owned_stage_root.empty());

		RecordingImmutablePublishFileOps file_ops(backing);
		file_ops.use_stage_tree_override = true;
		file_ops.stage_tree_override = existing;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		REQUIRE(cleanup_registry.TrackStageTree(
			existing.owned_stage_root, existing.file_identity));

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("remove-tree") == 0);
		CHECK(cleanup_registry.OwnsStageTree(existing.owned_stage_root));
		const AshEngine::VegetationFileInspection preserved = backing.InspectPath(
			root.Path(), existing.owned_stage_root.lexically_relative(absolute_root));
		REQUIRE(preserved.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.exists);
		CHECK_FALSE(preserved.is_regular_file);
		CHECK(cleanup_registry.CleanupStageTree(existing.owned_stage_root, file_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("a successful result outside the requested staging parent is left untouched")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-tree-parent");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::IVegetationFileOps& backing =
			AshEngine::get_default_vegetation_file_ops();
		REQUIRE(backing.EnsureDirectoryTree(root.Path(), "unrelated-staging") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		const std::filesystem::path unrelated_staging =
			std::filesystem::absolute(
				root.Path() / "unrelated-staging").lexically_normal();
		const AshEngine::VegetationStageTreeResult unrelated =
			backing.CreateUniqueStageTree(
				unrelated_staging,
				fixture.Transaction().expected_identity.operation_serial + 2000);
		REQUIRE(unrelated.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE_FALSE(unrelated.owned_stage_root.empty());

		RecordingImmutablePublishFileOps file_ops(backing);
		file_ops.use_stage_tree_override = true;
		file_ops.stage_tree_override = unrelated;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("remove-tree") == 0);
		CHECK(cleanup_registry.empty());
		const std::filesystem::path absolute_root =
			std::filesystem::absolute(root.Path()).lexically_normal();
		const AshEngine::VegetationFileInspection preserved = backing.InspectPath(
			root.Path(), unrelated.owned_stage_root.lexically_relative(absolute_root));
		REQUIRE(preserved.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.exists);
		CHECK_FALSE(preserved.is_regular_file);
		CHECK(backing.RemoveOwnedStageTree(
			unrelated.owned_stage_root, unrelated.file_identity));
	}
}

TEST_CASE("Vegetation chunk set prepare rejects an untrusted manifest before filesystem mutation")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-untrusted");
	NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	AshEngine::VegetationChunkSetManifestEntry extra =
		fixture.Transaction().resulting_manifest.entries[0];
	extra.coord = { 1, 0 };
	extra.object_sha256.fill(0x71);
	extra.input_sha256.fill(0x81);
	fixture.baked.transaction->resulting_manifest.entries.push_back(extra);

	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	CHECK(file_ops.events.empty());
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare rejects an out-of-root layer inspection before mutation")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-layer-resolution");
	const NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.rewrite_resolved_inspection_call = 1;
	file_ops.rewritten_resolved_path =
		(root.Path().parent_path() / "outside.AshVegetationLayer").lexically_normal();
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::InvalidPath);
	CHECK(file_ops.EventNames() == std::vector<std::string>{ "inspect" });
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare rejects noncanonical layer inputs before provider access")
{
	SUBCASE("dot components are not normalized on behalf of the caller")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-layer-dot");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), "vegetation/./meadow.AshVegetationLayer",
				fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::InvalidPath);
		CHECK(file_ops.events.empty());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("rooted paths are rejected instead of being reinterpreted")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-layer-rooted");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), root.Path() / fixture.layer_relative_path,
				fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::InvalidPath);
		CHECK(file_ops.events.empty());
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("parent traversal cannot escape a nested asset root")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-prepare-layer-parent");
		const NoActivePrepareFixture fixture{};
		const std::filesystem::path nested_root = root.Path() / "asset-root";
		REQUIRE(AshEngine::get_default_vegetation_file_ops().EnsureDirectoryTree(
			root.Path(), "asset-root") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		root.Write("outside.AshVegetationLayer",
			EncodeLayerOrThrow(*fixture.input.layer_snapshot));
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				nested_root, "../outside.AshVegetationLayer", fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::InvalidPath);
		CHECK(file_ops.events.empty());
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare rejects a cooker identity forged outside ASVC")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-forged-cooker");
	NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	++fixture.baked.transaction->expected_identity.cooker_version;
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	CHECK(file_ops.events.empty());
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare rejects an unsupported cooker on empty output")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-empty-forged-cooker");
	const std::filesystem::path layer_relative_path =
		"vegetation/meadow.AshVegetationLayer";
	const AshEngine::VegetationBakeInput input = EmptyPaletteBakeInput(7);
	root.Write(layer_relative_path, EncodeLayerOrThrow(*input.layer_snapshot));
	AshEngine::VegetationBakeResult baked = AshEngine::bake_vegetation_chunks(
		input, VegetationTest::ActiveControl(std::chrono::seconds(1)));
	REQUIRE(baked.status == AshEngine::VegetationBakeStatus::Succeeded);
	REQUIRE(baked.transaction.has_value());
	REQUIRE(baked.transaction->chunks.empty());
	++baked.transaction->expected_identity.cooker_version;
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), layer_relative_path, baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	CHECK(file_ops.events.empty());
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare retains a verified active sibling when post-create inspection throws")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-active-inspect-throw");
	const NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.throw_inspection_call = 6;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() ==
		AshEngine::VegetationChunkSetPrepareStatus::RecoveryRequired);
	REQUIRE_FALSE(file_ops.sibling_stage.empty());
	CHECK(prepared.stage_path() == file_ops.sibling_stage);
	CHECK(prepared.stage_file_identity().available);
	CHECK(file_ops.EventCount("remove-file") == 0);
	CHECK(cleanup_registry.IsRecoveryStageFile(file_ops.sibling_stage));
	const AshEngine::VegetationFileInspection sibling =
		AshEngine::get_default_vegetation_file_ops().InspectPath(
			root.Path(), fixture.store_relative_path / file_ops.sibling_stage.filename());
	REQUIRE(sibling.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(sibling.exists);
	CHECK(sibling.is_regular_file);
	CHECK(sibling.resolved_absolute_path == file_ops.sibling_stage);
	CHECK(sibling.file_identity.volume_serial_number ==
		file_ops.sibling_identity.volume_serial_number);
	CHECK(sibling.file_identity.file_index == file_ops.sibling_identity.file_index);
	CHECK(cleanup_registry.ReleaseRecoveryStageFile(file_ops.sibling_stage));
	CHECK(cleanup_registry.CleanupStageFile(file_ops.sibling_stage, file_ops));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation chunk set prepare never owns or deletes an active stage that aliases its target")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-prepare-active-alias");
	const NoActivePrepareFixture fixture{};
	fixture.WriteLayer(root);
	RecordingImmutablePublishFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.alias_active_to_sibling_inspection_call = 6;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedChunkSet prepared =
		AshEngine::prepare_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path, fixture.baked,
			VegetationTest::ActiveControl(std::chrono::seconds(2)),
			cleanup_registry, file_ops);
	CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
	REQUIRE_FALSE(file_ops.sibling_stage.empty());
	CHECK(prepared.stage_path().empty());
	CHECK(file_ops.EventCount("remove-file") == 0);
	CHECK(cleanup_registry.empty());
	const AshEngine::VegetationFileInspection sibling =
		AshEngine::get_default_vegetation_file_ops().InspectPath(
			root.Path(), fixture.store_relative_path / file_ops.sibling_stage.filename());
	REQUIRE(sibling.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(sibling.exists);
		CHECK(sibling.is_regular_file);
		CHECK(AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageFile(
			file_ops.sibling_stage, sibling.file_identity));
}

TEST_CASE("Vegetation chunk set prepare admits only legal directories and stage trees")
{
	struct AdmissionFaultCase
	{
		const char* slug = nullptr;
		const char* event = nullptr;
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFailed;
		size_t expected_ensure_count = 0;
		bool use_empty_tree_override = false;
		bool expect_unclaimed_tree = false;
	};
	const std::array<AdmissionFaultCase, 7> cases = {{
		{ "ensure-1", "ensure", 1, PrepareFaultMode::ReturnFailed, 1, false, false },
		{ "ensure-2", "ensure", 2, PrepareFaultMode::ReturnFailed, 2, false, false },
		{ "ensure-3", "ensure", 3, PrepareFaultMode::ReturnFailed, 3, false, false },
		{ "ensure-4", "ensure", 4, PrepareFaultMode::ReturnFailed, 4, false, false },
		{ "tree-failed", "create-tree", 1, PrepareFaultMode::ReturnFailed,
			4, false, false },
		{ "tree-failed-with-path", "create-tree", 1,
			PrepareFaultMode::IllegalPayload, 4, false, true },
		{ "tree-succeeded-empty", "create-tree", 1,
			PrepareFaultMode::IllegalPayload, 4, true, false }
	}};

	for (const AdmissionFaultCase& fault_case : cases)
	{
		CAPTURE(std::string(fault_case.slug));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-prepare-admission-") + fault_case.slug);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.use_stage_tree_override = fault_case.use_empty_tree_override;
		file_ops.stage_tree_override = {};
		file_ops.fault_rules.push_back(
			{ fault_case.event, fault_case.occurrence, fault_case.mode });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("ensure") == fault_case.expected_ensure_count);
		CHECK_FALSE(file_ops.HasEvent("create-child"));
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		CHECK(std::count(file_ops.observed_fault_events.begin(),
			file_ops.observed_fault_events.end(), fault_case.event) ==
			fault_case.occurrence);
		if (fault_case.expect_unclaimed_tree)
		{
			CHECK(file_ops.EventCount("remove-tree") == 0);
			REQUIRE_FALSE(file_ops.stage_tree.empty());
			const std::filesystem::path tree_relative =
				file_ops.stage_tree.lexically_relative(root.Path());
			REQUIRE_FALSE(tree_relative.empty());
			const AshEngine::VegetationFileInspection retained =
				AshEngine::get_default_vegetation_file_ops().InspectPath(
					root.Path(), tree_relative);
			REQUIRE(retained.status ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			CHECK(retained.exists);
			CHECK_FALSE(retained.is_regular_file);
			CHECK(AshEngine::get_default_vegetation_file_ops().RemoveOwnedStageTree(
				file_ops.stage_tree, file_ops.stage_tree_identity));
			const AshEngine::VegetationFileInspection removed =
				AshEngine::get_default_vegetation_file_ops().InspectPath(
					root.Path(), tree_relative);
			REQUIRE(removed.status ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			CHECK_FALSE(removed.exists);
			CHECK_FALSE(removed.is_regular_file);
		}
	}
}

TEST_CASE("Vegetation chunk set prepare requires legal stage tree identity shapes")
{
	struct IllegalStageTreeIdentityCase
	{
		const char* slug = nullptr;
		bool succeeded_without_identity = false;
		AshEngine::VegetationFileIdentity failed_identity{};
	};
	const std::array<IllegalStageTreeIdentityCase, 4> cases = {{
		{ "succeeded-without-identity", true, {} },
		{ "failed-with-available-identity", false,
			{ true, 0x1234u, 0x5678u } },
		{ "failed-with-unavailable-volume", false,
			{ false, 0x1234u, 0u } },
		{ "failed-with-unavailable-index", false,
			{ false, 0u, 0x5678u } }
	}};
	for (const IllegalStageTreeIdentityCase& test_case : cases)
	{
		CAPTURE(std::string(test_case.slug));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-stage-tree-") + test_case.slug);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::IVegetationFileOps& backing =
			AshEngine::get_default_vegetation_file_ops();
		RecordingImmutablePublishFileOps file_ops(backing);
		file_ops.use_stage_tree_override = true;
		AshEngine::VegetationStageTreeResult created{};
		if (test_case.succeeded_without_identity)
		{
			const std::filesystem::path staging_relative =
				fixture.store_relative_path / "staging";
			REQUIRE(backing.EnsureDirectoryTree(root.Path(), staging_relative) ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			const std::filesystem::path staging_absolute =
				(std::filesystem::absolute(root.Path()).lexically_normal() /
					staging_relative).lexically_normal();
			created = backing.CreateUniqueStageTree(
				staging_absolute,
				fixture.Transaction().expected_identity.operation_serial + 3000);
			REQUIRE(created.status == AshEngine::VegetationFileResultStatus::Succeeded);
			REQUIRE(created.file_identity.available);
			file_ops.stage_tree_override = created;
			file_ops.stage_tree_override.file_identity = {};
		}
		else
		{
			file_ops.stage_tree_override.status =
				AshEngine::VegetationFileResultStatus::Failed;
			file_ops.stage_tree_override.file_identity = test_case.failed_identity;
			file_ops.stage_tree_override.error = "provider failure";
		}
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() == AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(prepared.error().find("illegal shape") != std::string::npos);
		CHECK_FALSE(file_ops.HasEvent("create-child"));
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		if (test_case.succeeded_without_identity)
		{
			CHECK(file_ops.EventCount("remove-tree") == 0);
			CHECK(backing.RemoveOwnedStageTree(
				created.owned_stage_root, created.file_identity));
		}
	}
}

TEST_CASE("Vegetation chunk set prepare contains immutable object and manifest faults")
{
	struct ImmutableFaultCase
	{
		const char* label = nullptr;
		const char* event = nullptr;
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFailed;
		bool expect_object = false;
		bool expect_manifest = false;
		bool expect_invalid_path = false;
	};
	const std::array<ImmutableFaultCase, 15> cases = {{
		{ "object-create", "create-child", 1, PrepareFaultMode::ReturnInvalidPath,
			false, false, true },
		{ "object-write", "object.write", 1, PrepareFaultMode::ReturnFalse,
			false, false },
		{ "object-flush", "object.flush", 1, PrepareFaultMode::ReturnFalse,
			false, false },
		{ "object-stage-read", "read", 1, PrepareFaultMode::ReturnFailed,
			false, false },
		{ "object-stage-read-throw", "read", 1, PrepareFaultMode::Throw,
			false, false },
		{ "object-publish", "publish", 1, PrepareFaultMode::ReturnFailed,
			false, false },
		{ "object-target-inspect", "inspect", 3, PrepareFaultMode::ReturnFailed,
			true, false },
		{ "object-target-read", "read", 2, PrepareFaultMode::CorruptBytes,
			true, false },
		{ "manifest-create", "create-child", 2,
			PrepareFaultMode::ReturnInvalidPath, true, false, true },
		{ "manifest-write", "manifest.write", 1, PrepareFaultMode::ReturnFalse,
			true, false },
		{ "manifest-flush", "manifest.flush", 1, PrepareFaultMode::ReturnFalse,
			true, false },
		{ "manifest-stage-read", "read", 3, PrepareFaultMode::ReturnFailed,
			true, false },
		{ "manifest-publish", "publish", 2, PrepareFaultMode::ReturnFailed,
			true, false },
		{ "manifest-target-inspect", "inspect", 4,
			PrepareFaultMode::ReturnFailed, true, true },
		{ "manifest-target-read", "read", 4, PrepareFaultMode::CorruptBytes,
			true, true }
	}};

	for (const ImmutableFaultCase& fault_case : cases)
	{
		CAPTURE(std::string(fault_case.label));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-immutable-fault-") + fault_case.label);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules.push_back(
			{ fault_case.event, fault_case.occurrence, fault_case.mode });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
		CHECK_NOTHROW(prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops)));
		REQUIRE(prepared.has_value());
		CHECK(prepared->status() == (fault_case.expect_invalid_path
			? AshEngine::VegetationChunkSetPrepareStatus::InvalidPath
			: AshEngine::VegetationChunkSetPrepareStatus::Failed));
		CHECK(std::count(file_ops.observed_fault_events.begin(),
			file_ops.observed_fault_events.end(), fault_case.event) ==
			fault_case.occurrence);
		CHECK(file_ops.EventCount("remove-tree") == 1);
		CHECK_FALSE(file_ops.stage_tree.empty());
		CHECK(cleanup_registry.empty());
		const auto removed_tree = std::find_if(
			file_ops.events.begin(), file_ops.events.end(),
			[](const PrepareFileOpEvent& event)
			{
				return event.name == "remove-tree";
			});
		REQUIRE(removed_tree != file_ops.events.end());
		CHECK(removed_tree->path == file_ops.stage_tree);
		for (const std::filesystem::path& child : file_ops.child_stage_paths)
		{
			CHECK(child.parent_path() == file_ops.stage_tree);
			CHECK_FALSE(cleanup_registry.OwnsStageFile(child));
		}

		auto check_target = [&](const std::filesystem::path& relative_path,
			const std::vector<uint8_t>& expected_bytes, const bool expected)
		{
			const AshEngine::VegetationFileInspection inspection =
				AshEngine::get_default_vegetation_file_ops().InspectPath(
					root.Path(), relative_path);
			CHECK(inspection.status ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			CHECK(inspection.exists == expected);
			CHECK(inspection.is_regular_file == expected);
			if (expected)
			{
				const AshEngine::VegetationFileBytesResult read =
					AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
						inspection.resolved_absolute_path, expected_bytes.size());
				REQUIRE(read.status ==
					AshEngine::VegetationFileResultStatus::Succeeded);
				CHECK(read.bytes == expected_bytes);
			}
		};
		check_target(fixture.object_relative_path,
			fixture.Transaction().chunks.front().object_bytes,
			fault_case.expect_object);
		check_target(fixture.manifest_relative_path, fixture.manifest_bytes,
			fault_case.expect_manifest);
		const AshEngine::VegetationFileInspection active =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), fixture.active_relative_path);
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK_FALSE(active.exists);
		CHECK_FALSE(active.is_regular_file);
	}
}

TEST_CASE("Vegetation chunk set prepare control stops at exact write boundaries")
{
	struct PreControlCase
	{
		const char* label = nullptr;
		AshEngine::VegetationChunkSetPrepareStatus expected =
			AshEngine::VegetationChunkSetPrepareStatus::Failed;
		bool request_cancel = false;
		bool clear_cancel = false;
		bool clear_deadline = false;
	};
	const std::array<PreControlCase, 4> pre_control_cases = {{
		{ "cancelled", AshEngine::VegetationChunkSetPrepareStatus::Cancelled,
			true, false, false },
		{ "timed-out", AshEngine::VegetationChunkSetPrepareStatus::TimedOut,
			false, false, false },
		{ "null-cancel", AshEngine::VegetationChunkSetPrepareStatus::Failed,
			false, true, false },
		{ "default-deadline", AshEngine::VegetationChunkSetPrepareStatus::Failed,
			false, false, true }
	}};
	for (const PreControlCase& control_case : pre_control_cases)
	{
		CAPTURE(std::string(control_case.label));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-prepare-pre-") + control_case.label);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::seconds(2));
		if (control_case.request_cancel)
		{
			std::const_pointer_cast<std::atomic_bool>(control.cancel_requested)->store(
				true, std::memory_order_release);
		}
		else if (std::string_view(control_case.label) == "timed-out")
		{
			control.deadline = std::chrono::steady_clock::now() -
				std::chrono::milliseconds(1);
		}
		if (control_case.clear_cancel)
		{
			control.cancel_requested.reset();
		}
		if (control_case.clear_deadline)
		{
			control.deadline = {};
		}
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked, control,
				cleanup_registry, file_ops);
		CHECK(prepared.status() == control_case.expected);
		CHECK_FALSE(prepared.error().empty());
		CHECK(prepared.stage_path().empty());
		CHECK(file_ops.events.empty());
		CHECK(file_ops.observed_fault_events.empty());
		CHECK(cleanup_registry.empty());
	}

	struct WriteCancelCase
	{
		const char* event = nullptr;
		bool expect_object = false;
		bool expect_manifest = false;
		bool expect_active_cleanup = false;
	};
	const std::array<WriteCancelCase, 3> write_cases = {{
		{ "object.write", false, false, false },
		{ "manifest.write", true, false, false },
		{ "active.write", true, true, true }
	}};
	for (const WriteCancelCase& write_case : write_cases)
	{
		CAPTURE(std::string(write_case.event));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-prepare-cancel-") + write_case.event);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::seconds(2));
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.fault_rules.push_back(
			{ write_case.event, 1, PrepareFaultMode::CancelAfterSuccess });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked, control,
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Cancelled);
		CHECK(file_ops.EventCount("remove-tree") == 1);
		CHECK(file_ops.EventCount("remove-file") ==
			(write_case.expect_active_cleanup ? 1 : 0));
		CHECK(cleanup_registry.empty());
		auto inspect_target = [&](const std::filesystem::path& relative_path)
		{
			return AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), relative_path);
		};
		const AshEngine::VegetationFileInspection object =
			inspect_target(fixture.object_relative_path);
		const AshEngine::VegetationFileInspection manifest =
			inspect_target(fixture.manifest_relative_path);
		const AshEngine::VegetationFileInspection active =
			inspect_target(fixture.active_relative_path);
		REQUIRE(object.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(manifest.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(active.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(object.exists == write_case.expect_object);
		CHECK(object.is_regular_file == write_case.expect_object);
		CHECK(manifest.exists == write_case.expect_manifest);
		CHECK(manifest.is_regular_file == write_case.expect_manifest);
		CHECK_FALSE(active.exists);
		CHECK_FALSE(active.is_regular_file);
	}
}

TEST_CASE("Vegetation chunk set prepare rechecks control after active flush and final readback")
{
	enum class StopPoint : uint8_t
	{
		FlushCancellation,
		ReadCancellation,
		ReadDeadline
	};
	struct StopCase
	{
		const char* label = nullptr;
		StopPoint stop_point = StopPoint::FlushCancellation;
		AshEngine::VegetationChunkSetPrepareStatus expected =
			AshEngine::VegetationChunkSetPrepareStatus::Failed;
		size_t expected_reads = 0;
	};
	const std::array<StopCase, 3> cases{ {
		{ "flush-cancel", StopPoint::FlushCancellation,
			AshEngine::VegetationChunkSetPrepareStatus::Cancelled, 4 },
		{ "read-cancel", StopPoint::ReadCancellation,
			AshEngine::VegetationChunkSetPrepareStatus::Cancelled, 5 },
		{ "read-deadline", StopPoint::ReadDeadline,
			AshEngine::VegetationChunkSetPrepareStatus::TimedOut, 5 }
	} };

	for (const StopCase& stop_case : cases)
	{
		CAPTURE(std::string(stop_case.label));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-prepare-final-control-") + stop_case.label);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::seconds(2));
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		switch (stop_case.stop_point)
		{
		case StopPoint::FlushCancellation:
			file_ops.fault_rules.push_back(
				{ "active.flush", 1, PrepareFaultMode::CancelAfterSuccess });
			break;
		case StopPoint::ReadCancellation:
			file_ops.fault_rules.push_back(
				{ "read", 5, PrepareFaultMode::CancelAfterSuccess });
			break;
		case StopPoint::ReadDeadline:
			control.deadline = std::chrono::steady_clock::now() +
				std::chrono::milliseconds(250);
			file_ops.read_result_hook = [&control, &file_ops](
				const std::filesystem::path& path,
				const uint64_t,
				AshEngine::VegetationFileBytesResult& result)
			{
				if (path == file_ops.sibling_stage &&
					result.status == AshEngine::VegetationFileResultStatus::Succeeded)
				{
					while (std::chrono::steady_clock::now() <= control.deadline)
					{
						std::this_thread::yield();
					}
				}
			};
			break;
		}

		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked, control,
				cleanup_registry, file_ops);

		CHECK(prepared.status() == stop_case.expected);
		CHECK_FALSE(prepared.error().empty());
		CHECK(prepared.stage_path().empty());
		CHECK_FALSE(prepared.stage_file_identity().available);
		CHECK(prepared.stage_file_identity().volume_serial_number == 0);
		CHECK(prepared.stage_file_identity().file_index == 0);
		CHECK(prepared.active_stage_size() == 0);
		CHECK(prepared.active_stage_sha256() == AshEngine::VegetationSha256{});
		CHECK(file_ops.EventCount("active.flush") == 1);
		CHECK(file_ops.EventCount("read") == stop_case.expected_reads);
		CHECK(file_ops.EventCount("remove-file") == 1);
		CHECK(cleanup_registry.empty());
		REQUIRE_FALSE(file_ops.sibling_stage.empty());
		const AshEngine::VegetationFileBytesResult removed =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				file_ops.sibling_stage, 48);
		CHECK(removed.status == AshEngine::VegetationFileResultStatus::NotFound);
		CHECK(removed.bytes.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare contains active-stage faults and ownership conflicts")
{
	struct ActiveFaultCase
	{
		const char* label = nullptr;
		const char* event = nullptr;
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFailed;
		AshEngine::VegetationChunkSetPrepareStatus expected =
			AshEngine::VegetationChunkSetPrepareStatus::Failed;
		bool expect_sibling = false;
	};
	const std::array<ActiveFaultCase, 6> cases = {{
		{ "initial-inspect", "inspect", 5, PrepareFaultMode::Throw,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, false },
		{ "create", "create-sibling", 1, PrepareFaultMode::ReturnInvalidPath,
			AshEngine::VegetationChunkSetPrepareStatus::InvalidPath, false },
		{ "write", "active.write", 1, PrepareFaultMode::ReturnFalse,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, true },
		{ "flush", "active.flush", 1, PrepareFaultMode::ReturnFalse,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, true },
		{ "readback", "read", 5, PrepareFaultMode::CorruptBytes,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, true },
		{ "readback-throw", "read", 5, PrepareFaultMode::Throw,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, true }
	}};
	for (const ActiveFaultCase& fault_case : cases)
	{
		CAPTURE(std::string(fault_case.label));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-active-fault-") + fault_case.label);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules.push_back(
			{ fault_case.event, fault_case.occurrence, fault_case.mode });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
		CHECK_NOTHROW(prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops)));
		REQUIRE(prepared.has_value());
		CHECK(prepared->status() == fault_case.expected);
		CHECK(file_ops.EventCount("remove-tree") == 1);
		CHECK(file_ops.EventCount("remove-file") ==
			(fault_case.expect_sibling ? 1 : 0));
		if (fault_case.expect_sibling)
		{
			const auto removed = std::find_if(
				file_ops.events.begin(), file_ops.events.end(),
				[](const PrepareFileOpEvent& event)
				{
					return event.name == "remove-file";
				});
			REQUIRE(removed != file_ops.events.end());
			CHECK(removed->path == file_ops.sibling_stage);
		}
		CHECK(prepared->stage_path().empty());
		CHECK(cleanup_registry.empty());
	}

	for (const auto& cleanup_case : std::array{
			std::pair{ "return-false", PrepareFaultMode::ReturnFalse },
			std::pair{ "throw", PrepareFaultMode::Throw } })
	{
		CAPTURE(std::string(cleanup_case.first));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-active-cleanup-") + cleanup_case.first);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules = {
			{ "active.write", 1, PrepareFaultMode::ReturnFalse },
			{ "remove-file", 1, cleanup_case.second }
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
		CHECK_NOTHROW(prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops)));
		REQUIRE(prepared.has_value());
		CHECK(prepared->status() ==
			AshEngine::VegetationChunkSetPrepareStatus::RecoveryRequired);
		REQUIRE_FALSE(file_ops.sibling_stage.empty());
		CHECK(prepared->stage_path() == file_ops.sibling_stage);
		CHECK(cleanup_registry.OwnsStageFile(file_ops.sibling_stage));
		CHECK(file_ops.EventCount("remove-file") == 1);
		const std::filesystem::path sibling_relative =
			file_ops.sibling_stage.lexically_relative(root.Path());
		REQUIRE_FALSE(sibling_relative.empty());
		const AshEngine::VegetationFileInspection retained =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), sibling_relative);
		REQUIRE(retained.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained.exists);
		CHECK(retained.is_regular_file);
		CHECK(cleanup_registry.CleanupStageFile(file_ops.sibling_stage, file_ops));
		CHECK(file_ops.EventCount("remove-file") == 2);
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileInspection removed =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), sibling_relative);
		REQUIRE(removed.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK_FALSE(removed.exists);
		CHECK_FALSE(removed.is_regular_file);
	}

	{
		CAPTURE("track-conflict");
		VegetationTest::ScopedAssetRoot root("chunk-set-active-track-conflict");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		bool callback_called = false;
		bool externally_tracked = false;
		file_ops.after_success = [&](const std::string_view event,
			const size_t occurrence)
		{
			if (event == "create-sibling" && occurrence == 1)
			{
				callback_called = true;
				externally_tracked =
					cleanup_registry.TrackStageFile(
						file_ops.sibling_stage, file_ops.sibling_identity);
			}
		};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(callback_called);
		CHECK(externally_tracked);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(prepared.stage_path().empty());
		CHECK(file_ops.EventCount("remove-file") == 0);
		CHECK(cleanup_registry.OwnsStageFile(file_ops.sibling_stage));
		CHECK(cleanup_registry.CleanupStageFile(file_ops.sibling_stage, file_ops));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare retains only a fully-published tree on cleanup failure")
{
	for (const auto& cleanup_case : std::array{
			std::pair{ "return-false", PrepareFaultMode::ReturnFalse },
			std::pair{ "throw", PrepareFaultMode::Throw } })
	{
		CAPTURE(std::string(cleanup_case.first));
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-final-tree-cleanup-") + cleanup_case.first);
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules.push_back(
			{ "remove-tree", 1, cleanup_case.second });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
		CHECK_NOTHROW(prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops)));
		REQUIRE(prepared.has_value());
		CHECK(prepared->status() ==
			AshEngine::VegetationChunkSetPrepareStatus::RecoveryRequired);
		REQUIRE(file_ops.publish_results.size() == 2);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.sibling_stage.empty());
		CHECK(prepared->stage_path().empty());
		CHECK(cleanup_registry.OwnsStageTree(file_ops.stage_tree));
		CHECK(file_ops.EventCount("remove-tree") == 1);
		const std::filesystem::path tree_relative =
			file_ops.stage_tree.lexically_relative(root.Path());
		REQUIRE_FALSE(tree_relative.empty());
		const AshEngine::VegetationFileInspection retained =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), tree_relative);
		REQUIRE(retained.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(retained.exists);
		CHECK_FALSE(retained.is_regular_file);
		CHECK(cleanup_registry.CleanupStageTree(file_ops.stage_tree, file_ops));
		CHECK(file_ops.EventCount("remove-tree") == 2);
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileInspection removed =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), tree_relative);
		REQUIRE(removed.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK_FALSE(removed.exists);
		CHECK_FALSE(removed.is_regular_file);
	}
}

TEST_CASE("Vegetation chunk set prepare preserves canonical multi-object prefix semantics")
{
	struct SecondObjectFaultCase
	{
		const char* label = nullptr;
		const char* event = nullptr;
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFailed;
		bool expect_second_object = false;
	};
	const std::array<SecondObjectFaultCase, 7> cases = {{
		{ "create", "create-child", 2, PrepareFaultMode::ReturnFailed, false },
		{ "write", "object.write", 2, PrepareFaultMode::ReturnFalse, false },
		{ "flush", "object.flush", 2, PrepareFaultMode::ReturnFalse, false },
		{ "stage-read", "read", 3, PrepareFaultMode::ReturnFailed, false },
		{ "publish", "publish", 2, PrepareFaultMode::ReturnFailed, false },
		{ "target-inspect", "inspect", 4, PrepareFaultMode::ReturnFailed, true },
		{ "target-read", "read", 4, PrepareFaultMode::CorruptBytes, true }
	}};

	for (const SecondObjectFaultCase& fault_case : cases)
	{
		CAPTURE(std::string(fault_case.label));
		auto surface = std::make_shared<BatchedSurfaceSnapshot>();
		const NoActivePrepareFixture fixture(TwoDirtyChunkBakeInput(surface));
		REQUIRE(fixture.Transaction().chunks.size() == 2);
		REQUIRE(fixture.object_relative_paths.size() == 2);
		CHECK(fixture.Transaction().chunks[0].coord.x == 0);
		CHECK(fixture.Transaction().chunks[1].coord.x == 1);
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-second-object-") + fault_case.label);
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules.push_back(
			{ fault_case.event, fault_case.occurrence, fault_case.mode });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(file_ops.EventCount("remove-tree") == 1);
		CHECK(file_ops.EventCount("manifest.write") == 0);
		CHECK_FALSE(file_ops.HasEvent("create-sibling"));
		CHECK(cleanup_registry.empty());
		for (size_t index = 0; index < 2; ++index)
		{
			const AshEngine::VegetationBakedChunk& chunk =
				fixture.Transaction().chunks[index];
			const bool expected = index == 0 || fault_case.expect_second_object;
			const AshEngine::VegetationFileInspection inspection =
				AshEngine::get_default_vegetation_file_ops().InspectPath(
					root.Path(), fixture.object_relative_paths[index]);
			REQUIRE(inspection.status ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			CHECK(inspection.exists == expected);
			CHECK(inspection.is_regular_file == expected);
			if (expected)
			{
				const AshEngine::VegetationFileBytesResult read =
					AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
						inspection.resolved_absolute_path, chunk.object_bytes.size());
				REQUIRE(read.status ==
					AshEngine::VegetationFileResultStatus::Succeeded);
				CHECK(read.bytes == chunk.object_bytes);
			}
		}
	}

	{
		CAPTURE("success-order");
		auto surface = std::make_shared<BatchedSurfaceSnapshot>();
		const NoActivePrepareFixture fixture(TwoDirtyChunkBakeInput(surface));
		VegetationTest::ScopedAssetRoot root("chunk-set-multi-object-order");
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		REQUIRE(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		CHECK(file_ops.EventCount("object.flush") == 2);
		CHECK(file_ops.EventCount("manifest.flush") == 1);
		std::vector<std::filesystem::path> published{};
		for (const PrepareFileOpEvent& event : file_ops.events)
		{
			if (event.name == "publish")
			{
				published.push_back(event.path);
			}
		}
		REQUIRE(published.size() == 3);
		CHECK(published[0] ==
			(root.Path() / fixture.object_relative_paths[0]).lexically_normal());
		CHECK(published[1] ==
			(root.Path() / fixture.object_relative_paths[1]).lexically_normal());
		CHECK(published[2] ==
			(root.Path() / fixture.manifest_relative_path).lexically_normal());
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation chunk set prepare streams strict large ASVC blocks without partial publish")
{
	auto make_fixture = []()
	{
		NoActivePrepareFixture fixture{};
		ExpandPrepareFixtureFirstObject(fixture, 120000);
		if (fixture.Transaction().chunks.front().object_bytes.size() <= 3u * 1024u * 1024u)
		{
			throw std::runtime_error("Large prepare fixture did not exceed three MiB");
		}
		return fixture;
	};

	{
		CAPTURE("success");
		NoActivePrepareFixture fixture = make_fixture();
		VegetationTest::ScopedAssetRoot root("chunk-set-large-block-success");
		fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(10)),
				cleanup_registry, file_ops);
		REQUIRE(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		std::vector<PrepareFileOpEvent> writes{};
		for (const PrepareFileOpEvent& event : file_ops.events)
		{
			if (event.name == "object.write")
			{
				writes.push_back(event);
			}
		}
		REQUIRE(writes.size() >= 4);
		uint64_t next_offset = 0;
		for (const PrepareFileOpEvent& write : writes)
		{
			CHECK(write.offset == next_offset);
			CHECK(write.value >= 1);
			CHECK(write.value <= 1024u * 1024u);
			next_offset += write.value;
		}
		CHECK(next_offset ==
			fixture.Transaction().chunks.front().object_bytes.size());
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), file_ops));
		CHECK(cleanup_registry.empty());
	}

	struct LargeFaultCase
	{
		const char* label = nullptr;
		size_t occurrence = 1;
		PrepareFaultMode mode = PrepareFaultMode::ReturnFalse;
		AshEngine::VegetationChunkSetPrepareStatus expected =
			AshEngine::VegetationChunkSetPrepareStatus::Failed;
		size_t expected_writes = 0;
	};
	const std::array<LargeFaultCase, 2> cases = {{
		{ "middle-block-failure", 3, PrepareFaultMode::ReturnFalse,
			AshEngine::VegetationChunkSetPrepareStatus::Failed, 3 },
		{ "after-middle-block-cancel", 2, PrepareFaultMode::CancelAfterSuccess,
			AshEngine::VegetationChunkSetPrepareStatus::Cancelled, 2 }
	}};
	for (const LargeFaultCase& fault_case : cases)
	{
		CAPTURE(std::string(fault_case.label));
		NoActivePrepareFixture fixture = make_fixture();
		VegetationTest::ScopedAssetRoot root(
			std::string("chunk-set-large-") + fault_case.label);
		fixture.WriteLayer(root);
		AshEngine::VegetationOperationControl control =
			VegetationTest::ActiveControl(std::chrono::seconds(5));
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.fault_rules.push_back(
			{ "object.write", fault_case.occurrence, fault_case.mode });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked, control,
				cleanup_registry, file_ops);
		CHECK(prepared.status() == fault_case.expected);
		CHECK(file_ops.EventCount("object.write") == fault_case.expected_writes);
		CHECK(file_ops.EventCount("publish") == 0);
		CHECK(file_ops.EventCount("remove-tree") == 1);
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileInspection object =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), fixture.object_relative_path);
		REQUIRE(object.status == AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK_FALSE(object.exists);
		CHECK_FALSE(object.is_regular_file);
	}
}

TEST_CASE("Vegetation chunk set prepare cooperates across operations and preserves the active winner")
{
	{
		CAPTURE("two-prepares");
		VegetationTest::ScopedAssetRoot root("chunk-set-cooperative-prepares");
		const NoActivePrepareFixture first_fixture{};
		NoActivePrepareFixture second_fixture{};
		REQUIRE(first_fixture.Transaction().expected_identity.operation_serial ==
			second_fixture.Transaction().expected_identity.operation_serial);
		REQUIRE(first_fixture.Transaction().chunks.front().object_bytes ==
			second_fixture.Transaction().chunks.front().object_bytes);
		REQUIRE(first_fixture.manifest_bytes == second_fixture.manifest_bytes);
		first_fixture.WriteLayer(root);
		RecordingImmutablePublishFileOps first_ops(
			AshEngine::get_default_vegetation_file_ops());
		RecordingImmutablePublishFileOps second_ops(
			AshEngine::get_default_vegetation_file_ops());
		AshEngine::VegetationOwnedStageCleanupRegistry first_registry{};
		AshEngine::VegetationOwnedStageCleanupRegistry second_registry{};

		const AshEngine::VegetationPreparedChunkSet first =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), first_fixture.layer_relative_path, first_fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				first_registry, first_ops);
		REQUIRE(first.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		const AshEngine::VegetationPreparedChunkSet second =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), second_fixture.layer_relative_path, second_fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				second_registry, second_ops);
		REQUIRE(second.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Prepared);
		REQUIRE(first_ops.publish_results.size() == 2);
		REQUIRE(second_ops.publish_results.size() == 2);
		CHECK(first_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(first_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(second_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK(second_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::AlreadyExists);
		CHECK(first_ops.stage_tree != second_ops.stage_tree);
		CHECK(first.stage_path() != second.stage_path());
		CHECK(first.stage_file_identity().available);
		CHECK(second.stage_file_identity().available);
		CHECK(first.stage_file_identity().file_index !=
			second.stage_file_identity().file_index);
		CHECK(first_registry.OwnsStageFile(first.stage_path()));
		CHECK_FALSE(first_registry.OwnsStageFile(second.stage_path()));
		CHECK(second_registry.OwnsStageFile(second.stage_path()));
		CHECK_FALSE(second_registry.OwnsStageFile(first.stage_path()));
		CHECK(first_registry.CleanupStageFile(first.stage_path(), first_ops));
		CHECK(first_registry.empty());
		CHECK(second_registry.OwnsStageFile(second.stage_path()));
		CHECK(second_registry.CleanupStageFile(second.stage_path(), second_ops));
		CHECK(second_registry.empty());
	}

	{
		CAPTURE("active-winner");
		VegetationTest::ScopedAssetRoot root("chunk-set-active-winner");
		const NoActivePrepareFixture fixture{};
		fixture.WriteLayer(root);
		AshEngine::VegetationChunkSetActivePointer winner{};
		winner.manifest_sha256.fill(0x5au);
		REQUIRE(winner.manifest_sha256 != fixture.manifest_sha256);
		std::vector<uint8_t> winner_bytes{};
		std::vector<uint8_t> candidate_bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_active_pointer(
			winner, winner_bytes, &error));
		AshEngine::VegetationChunkSetActivePointer candidate{};
		candidate.manifest_sha256 = fixture.manifest_sha256;
		REQUIRE(AshEngine::encode_vegetation_chunk_set_active_pointer(
			candidate, candidate_bytes, &error));
		REQUIRE(winner_bytes != candidate_bytes);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		bool winner_written = false;
		AshEngine::VegetationFileInspection winner_before{};
		file_ops.after_success = [&](const std::string_view event,
			const size_t occurrence)
		{
			if (event == "create-sibling" && occurrence == 1)
			{
				fixture.WriteAsset(root, fixture.active_relative_path, winner_bytes);
				winner_before = AshEngine::get_default_vegetation_file_ops().InspectPath(
					root.Path(), fixture.active_relative_path);
				winner_written = true;
			}
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedChunkSet prepared =
			AshEngine::prepare_vegetation_chunk_set(
				root.Path(), fixture.layer_relative_path, fixture.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops);
		CHECK(winner_written);
		CHECK(prepared.status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		CHECK(prepared.stage_path().empty());
		CHECK(file_ops.EventCount("remove-file") == 1);
		CHECK(cleanup_registry.empty());
		REQUIRE(winner_before.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(winner_before.exists);
		REQUIRE(winner_before.is_regular_file);
		REQUIRE(winner_before.file_identity.available);
		const AshEngine::VegetationFileInspection winner_after =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), fixture.active_relative_path);
		REQUIRE(winner_after.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(winner_after.exists);
		REQUIRE(winner_after.is_regular_file);
		REQUIRE(winner_after.file_identity.available);
		CHECK(winner_after.file_identity.volume_serial_number ==
			winner_before.file_identity.volume_serial_number);
		CHECK(winner_after.file_identity.file_index ==
			winner_before.file_identity.file_index);
		const AshEngine::VegetationFileBytesResult preserved =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				winner_after.resolved_absolute_path,
				winner_bytes.size());
		REQUIRE(preserved.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved.bytes == winner_bytes);
	}

	{
		CAPTURE("existing-lkg");
		VegetationTest::ScopedAssetRoot root("chunk-set-existing-lkg");
		const NoActivePrepareFixture old_lkg(
			SingleChunkBakeInput(0x1234u, 7));
		NoActivePrepareFixture candidate(
			SingleChunkBakeInput(0x1235u, 8));
		REQUIRE(old_lkg.Transaction().expected_identity.layer_generation !=
			candidate.Transaction().expected_identity.layer_generation);
		REQUIRE(old_lkg.manifest_sha256 != candidate.manifest_sha256);
		REQUIRE(old_lkg.Transaction().chunks.front().input_digest !=
			candidate.Transaction().chunks.front().input_digest);
		REQUIRE(old_lkg.Transaction().chunks.front().object_sha256 !=
			candidate.Transaction().chunks.front().object_sha256);
		candidate.WriteLayer(root);
		old_lkg.WriteAsset(root, old_lkg.object_relative_path,
			old_lkg.Transaction().chunks.front().object_bytes);
		old_lkg.WriteAsset(root, old_lkg.manifest_relative_path,
			old_lkg.manifest_bytes);
		AshEngine::VegetationChunkSetActivePointer pointer{};
		pointer.manifest_sha256 = old_lkg.manifest_sha256;
		std::vector<uint8_t> active_bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_active_pointer(
			pointer, active_bytes, &error));
		old_lkg.WriteAsset(root, old_lkg.active_relative_path, active_bytes);
		old_lkg.WriteAsset(root,
			old_lkg.input.layer_snapshot->palette.front().species_asset_path,
			VegetationTest::CanonicalGrassSpeciesJson());
		candidate.baked.transaction->source_active_identity.state =
			AshEngine::VegetationChunkSetSourceActiveState::Existing;
		candidate.baked.transaction->source_active_identity.manifest_sha256 =
			old_lkg.manifest_sha256;
		candidate.baked.transaction->full_rebake_required = true;
		const AshEngine::VegetationFileInspection active_before =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), old_lkg.active_relative_path);
		REQUIRE(active_before.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(active_before.exists);
		REQUIRE(active_before.is_regular_file);
		REQUIRE(active_before.file_identity.available);
		const auto resolver = CaptureFixtureResolver(root);
		RecordingImmutablePublishFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.fault_rules.push_back(
			{ "active.flush", 1, PrepareFaultMode::ReturnFalse });
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		std::optional<AshEngine::VegetationPreparedChunkSet> prepared{};
		CHECK_NOTHROW(prepared.emplace(AshEngine::prepare_vegetation_chunk_set(
				root.Path(), candidate.layer_relative_path, candidate.baked,
				VegetationTest::ActiveControl(std::chrono::seconds(2)),
				cleanup_registry, file_ops)));
		REQUIRE(prepared.has_value());
		CHECK(prepared->status() ==
			AshEngine::VegetationChunkSetPrepareStatus::Failed);
		REQUIRE(file_ops.publish_results.size() == 2);
		CHECK(file_ops.publish_results[0] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.publish_results[1] ==
			AshEngine::VegetationCreateNewStatus::Created);
		CHECK(file_ops.EventCount("active.write") == 1);
		CHECK(file_ops.EventCount("active.flush") == 1);
		CHECK(file_ops.EventCount("remove-file") == 1);
		CHECK(cleanup_registry.empty());
		const AshEngine::VegetationFileBytesResult candidate_object =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / candidate.object_relative_path).lexically_normal(),
				candidate.Transaction().chunks.front().object_bytes.size());
		REQUIRE(candidate_object.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(candidate_object.bytes ==
			candidate.Transaction().chunks.front().object_bytes);
		const AshEngine::VegetationFileBytesResult candidate_manifest =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				(root.Path() / candidate.manifest_relative_path).lexically_normal(),
				candidate.manifest_bytes.size());
		REQUIRE(candidate_manifest.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(candidate_manifest.bytes == candidate.manifest_bytes);
		const AshEngine::VegetationFileInspection active_after =
			AshEngine::get_default_vegetation_file_ops().InspectPath(
				root.Path(), old_lkg.active_relative_path);
		REQUIRE(active_after.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(active_after.exists);
		REQUIRE(active_after.is_regular_file);
		REQUIRE(active_after.file_identity.available);
		CHECK(active_after.file_identity.volume_serial_number ==
			active_before.file_identity.volume_serial_number);
		CHECK(active_after.file_identity.file_index ==
			active_before.file_identity.file_index);
		const AshEngine::VegetationFileBytesResult preserved_active =
			AshEngine::get_default_vegetation_file_ops().ReadAllBytes(
				active_after.resolved_absolute_path, active_bytes.size());
		REQUIRE(preserved_active.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		CHECK(preserved_active.bytes == active_bytes);
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.per_file = VegetationTest::GenerousLoadBudget();
		budget.max_manifest_entries = 1;
		budget.max_total_inspected_bytes = active_bytes.size() +
			old_lkg.manifest_bytes.size() +
			old_lkg.Transaction().chunks.front().object_bytes.size();
		budget.max_summary_bytes = 1024;
		const AshEngine::VegetationActiveChunkSetReadResult read =
			AshEngine::read_active_vegetation_chunk_set(
				root.Path(), old_lkg.layer_relative_path, *resolver,
				budget,
				VegetationTest::ActiveControl(std::chrono::seconds(2)));
		INFO(read.error);
		CHECK(read.status ==
			AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
		REQUIRE(read.snapshot);
		CHECK(read.snapshot->layer_generation == 7);
		CHECK(read.snapshot->layer_generation !=
			candidate.Transaction().expected_identity.layer_generation);
		CHECK(read.snapshot->manifest_sha256 == old_lkg.manifest_sha256);
		CHECK(read.snapshot->manifest_sha256 != candidate.manifest_sha256);
		REQUIRE(read.snapshot->entries.size() == 1);
		CHECK(read.snapshot->entries.front().object_sha256 ==
			old_lkg.Transaction().chunks.front().object_sha256);
		CHECK(read.snapshot->entries.front().object_sha256 !=
			candidate.Transaction().chunks.front().object_sha256);
		CHECK(read.snapshot->entries.front().input_sha256 ==
			old_lkg.Transaction().chunks.front().input_digest);
		CHECK(read.snapshot->entries.front().input_sha256 !=
			candidate.Transaction().chunks.front().input_digest);
	}
}

TEST_CASE("Vegetation chunk set read reports NoActive without publishing a partial snapshot")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-no-active");
	SUBCASE("zero file entry and inspection budgets are valid when no active pointer exists")
	{
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.max_summary_bytes = 112;
		const AshEngine::VegetationActiveChunkSetReadResult result =
			AshEngine::read_active_vegetation_chunk_set(
				root.Path(), "vegetation/meadow.AshVegetationLayer",
				AshEngine::VegetationAssetResolverSnapshot{}, budget,
				VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::NoActive);
		CHECK_FALSE(result.snapshot);
	}

	SUBCASE("summary budget below the fixed snapshot cost stays invalid")
	{
		AshEngine::VegetationChunkSetLoadBudget budget{};
		budget.max_summary_bytes = 111;
		const AshEngine::VegetationActiveChunkSetReadResult result =
			AshEngine::read_active_vegetation_chunk_set(
				root.Path(), "vegetation/meadow.AshVegetationLayer",
				AshEngine::VegetationAssetResolverSnapshot{}, budget,
				VegetationTest::ActiveControl(std::chrono::seconds(1)));
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
		CHECK_FALSE(result.snapshot);
	}
}

TEST_CASE("Vegetation chunk set read binds every inspection to its requested asset path")
{
	auto require_failed_before_read = [](
		const AshEngine::VegetationActiveChunkSetReadResult& result,
		const RecordingChunkSetReadFileOps& file_ops,
		const size_t expected_prior_reads)
	{
		CHECK(result.status != AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
		CHECK(result.status != AshEngine::VegetationActiveChunkSetReadStatus::NoActive);
		CHECK_FALSE(result.snapshot);
		CHECK(file_ops.read_calls.size() == expected_prior_reads);
	};

	SUBCASE("active canonical path substitution is rejected before bytes are read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-active-canonical-binding");
		const EmptyActiveChunkSetFixture fixture{};
		fixture.Write(root);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 1;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.active_relative_path);
		file_ops.inspection_override.canonical_relative_path =
			fixture.store_relative_path / "substituted.asva";

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 0);
	}

	SUBCASE("active absolute path escape is rejected before bytes are read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-active-absolute-binding");
		VegetationTest::ScopedAssetRoot outside("chunk-set-active-absolute-outside");
		const EmptyActiveChunkSetFixture fixture{};
		fixture.Write(root);
		fixture.WriteAsset(outside, fixture.active_relative_path, fixture.active_bytes);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 1;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.active_relative_path);
		file_ops.inspection_override.resolved_absolute_path =
			outside.Path() / fixture.active_relative_path;

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 0);
	}

	SUBCASE("manifest canonical path substitution is rejected before manifest read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-manifest-canonical-binding");
		const EmptyActiveChunkSetFixture fixture{};
		fixture.Write(root);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 2;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.manifest_relative_path);
		file_ops.inspection_override.canonical_relative_path =
			fixture.store_relative_path / "manifests" / "substituted.asvm";

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 1);
	}

	SUBCASE("manifest absolute path escape is rejected before manifest read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-manifest-absolute-binding");
		VegetationTest::ScopedAssetRoot outside("chunk-set-manifest-absolute-outside");
		const EmptyActiveChunkSetFixture fixture{};
		fixture.Write(root);
		fixture.WriteAsset(outside, fixture.manifest_relative_path, fixture.manifest_bytes);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 2;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.manifest_relative_path);
		file_ops.inspection_override.resolved_absolute_path =
			outside.Path() / fixture.manifest_relative_path;

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 1);
	}

	SUBCASE("object canonical path substitution is rejected before object read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-object-canonical-binding");
		const SingleObjectActiveChunkSetFixture fixture{};
		fixture.Write(root);
		const auto resolver = CaptureFixtureResolver(root);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 3;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.object_relative_path);
		file_ops.inspection_override.canonical_relative_path =
			fixture.chunk_set.store_relative_path / "objects" /
			"substituted.AshVegetationChunk";

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
			SingleObjectChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 2);
	}

	SUBCASE("object absolute path escape is rejected before object read")
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-object-absolute-binding");
		VegetationTest::ScopedAssetRoot outside("chunk-set-object-absolute-outside");
		const SingleObjectActiveChunkSetFixture fixture{};
		fixture.Write(root);
		fixture.chunk_set.WriteAsset(
			outside, fixture.object_relative_path, fixture.object_bytes);
		const auto resolver = CaptureFixtureResolver(root);
		auto& backing = AshEngine::get_default_vegetation_file_ops();
		RecordingChunkSetReadFileOps file_ops(backing);
		file_ops.overridden_inspection_call = 3;
		file_ops.inspection_override =
			backing.InspectPath(root.Path(), fixture.object_relative_path);
		file_ops.inspection_override.resolved_absolute_path =
			outside.Path() / fixture.object_relative_path;

		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
			SingleObjectChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
		require_failed_before_read(result, file_ops, 2);
	}
}

TEST_CASE("Vegetation chunk set read rejects noncanonical Layer paths at entry")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-noncanonical-layer-entry");
	RecordingChunkSetReadFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationChunkSetLoadBudget budget{};
	budget.max_summary_bytes = 112;

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), "vegetation/../Meadow.AshVegetationLayer",
		AshEngine::VegetationAssetResolverSnapshot{}, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);

	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK(file_ops.inspected_paths.empty());
	CHECK(file_ops.read_calls.empty());
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read loads a canonical empty active manifest from disk")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-empty-active");
	const EmptyActiveChunkSetFixture fixture{};
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());

	const AshEngine::VegetationActiveChunkSetReadResult result =
		AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);

	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
	REQUIRE(result.snapshot);
	CHECK(result.snapshot->layer_id == ManifestFixture(false).layer_id);
	CHECK(result.snapshot->layer_generation == ManifestFixture(false).layer_generation);
	CHECK(result.snapshot->surface_identity.surface_id ==
		ManifestFixture(false).surface_identity.surface_id);
	CHECK(result.snapshot->surface_identity.content_revision ==
		ManifestFixture(false).surface_identity.content_revision);
	CHECK(result.snapshot->surface_identity.residency_revision ==
		ManifestFixture(false).surface_identity.residency_revision);
	CHECK(result.snapshot->surface_identity.transform_revision ==
		ManifestFixture(false).surface_identity.transform_revision);
	CHECK(result.snapshot->manifest_sha256 == fixture.manifest_sha256);
	CHECK(result.snapshot->entries.empty());
	CHECK(result.store_relative_path == fixture.store_relative_path);
	CHECK(result.active_relative_path == fixture.active_relative_path);
	CHECK(result.error.empty());
	CHECK(file_ops.inspected_paths == std::vector<std::filesystem::path>{
		fixture.active_relative_path, fixture.manifest_relative_path });
	REQUIRE(file_ops.read_calls.size() == 2);
	CHECK(file_ops.read_calls[0].path == root.Path() / fixture.active_relative_path);
	CHECK(file_ops.read_calls[0].max_bytes == 96);
	CHECK(file_ops.read_calls[1].path == root.Path() / fixture.manifest_relative_path);
	CHECK(file_ops.read_calls[1].max_bytes == 96);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read supports a controlled path longer than MAX_PATH")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-controlled-long-path");
	const EmptyActiveChunkSetFixture fixture(true);
#if defined(_WIN32)
	CHECK((root.Path() / fixture.manifest_relative_path).native().size() > MAX_PATH);
#endif
	fixture.Write(root);

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)));
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
	REQUIRE(result.snapshot);
	CHECK(result.snapshot->manifest_sha256 == fixture.manifest_sha256);
	CHECK(result.store_relative_path == fixture.store_relative_path);
	CHECK(result.active_relative_path == fixture.active_relative_path);
	CHECK(result.error.empty());
}

TEST_CASE("Vegetation chunk set read rejects hash-consistent codec-invalid manifest")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-codec-invalid-manifest");
	EmptyActiveChunkSetFixture fixture{};
	WriteLiteralU32(fixture.manifest_bytes, 76, 1);
	RepairAsvmHeaderCrc(fixture.manifest_bytes);
	fixture.RefreshManifestPointer();
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.inspected_paths.size() == 2);
	CHECK(file_ops.read_calls.size() == 2);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read rejects a missing object from a one-entry manifest")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-non-empty-guard");
	EmptyActiveChunkSetFixture fixture{};
	fixture.manifest_bytes = SingleAsvmGolden();
	fixture.RefreshManifestPointer();
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	auto budget = EmptyChunkSetBudget();
	budget.per_file.max_file_bytes = 176;
	budget.max_total_inspected_bytes = 224;
	budget.max_manifest_entries = 1;
	budget.max_summary_bytes = 200;

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == 2);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read rejects missing or corrupt empty active data")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-invalid-active");
	EmptyActiveChunkSetFixture fixture{};

	SUBCASE("valid active pointer names a missing manifest")
	{
		fixture.Write(root, false);
	}

	SUBCASE("active pointer CRC is corrupt")
	{
		fixture.active_bytes.back() ^= 0x01u;
		fixture.Write(root);
	}

	SUBCASE("manifest bytes are corrupt under the original digest path")
	{
		fixture.Write(root);
		auto corrupt = fixture.manifest_bytes;
		corrupt.back() ^= 0x01u;
		fixture.WriteManifest(root, corrupt);
	}

	const AshEngine::VegetationActiveChunkSetReadResult result =
		AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
}

TEST_CASE("Vegetation chunk set empty active read enforces exact byte and summary budgets")
{
	auto ReadWithBudget = [](const AshEngine::VegetationChunkSetLoadBudget& budget)
	{
		VegetationTest::ScopedAssetRoot root("chunk-set-empty-budget");
		const EmptyActiveChunkSetFixture fixture{};
		fixture.Write(root);
		return AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, budget,
			VegetationTest::ActiveControl(std::chrono::seconds(1)));
	};

	SUBCASE("exact 96 per-file 144 total 112 summary and zero entries pass")
	{
		const auto result = ReadWithBudget(EmptyChunkSetBudget());
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
		REQUIRE(result.snapshot);
		CHECK(result.snapshot->entries.empty());
		CHECK(result.error.empty());
	}

	SUBCASE("per-file budget 95 rejects the 96-byte manifest")
	{
		auto budget = EmptyChunkSetBudget();
		budget.per_file.max_file_bytes = 95;
		const auto result = ReadWithBudget(budget);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.error.empty());
	}

	SUBCASE("total inspected budget 143 rejects the second snapshot")
	{
		auto budget = EmptyChunkSetBudget();
		budget.max_total_inspected_bytes = 143;
		const auto result = ReadWithBudget(budget);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.error.empty());
	}

	SUBCASE("summary budget 111 rejects the fixed empty snapshot")
	{
		auto budget = EmptyChunkSetBudget();
		budget.max_summary_bytes = 111;
		const auto result = ReadWithBudget(budget);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.error.empty());
	}
}

TEST_CASE("Vegetation chunk set active read stops at read boundaries and rejects illegal byte payloads")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-read-boundaries");
	const EmptyActiveChunkSetFixture fixture{};
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	auto cancellation = std::make_shared<std::atomic_bool>(false);
	AshEngine::VegetationOperationControl control{};
	control.cancel_requested = cancellation;
	control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	file_ops.cancellation = cancellation;

	SUBCASE("cancellation after ASVA read prevents manifest inspection")
	{
		file_ops.cancel_after_successful_read = 1;
		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			control, file_ops);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Cancelled);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.error.empty());
		CHECK(file_ops.inspected_paths.size() == 1);
		CHECK(file_ops.read_calls.size() == 1);
	}

	SUBCASE("cancellation after ASVM read prevents snapshot publication")
	{
		file_ops.cancel_after_successful_read = 2;
		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			control, file_ops);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Cancelled);
		CHECK_FALSE(result.snapshot);
		CHECK_FALSE(result.error.empty());
		CHECK(file_ops.inspected_paths.size() == 2);
		CHECK(file_ops.read_calls.size() == 2);
	}

	SUBCASE("non-success byte result carrying payload is an illegal provider shape")
	{
		file_ops.illegal_bytes_on_read = 1;
		const auto result = AshEngine::read_active_vegetation_chunk_set(
			root.Path(), fixture.layer_relative_path,
			AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
			control, file_ops);
		CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Failed);
		CHECK_FALSE(result.snapshot);
		CHECK(result.error.find("illegal status-payload shape") != std::string::npos);
		CHECK(file_ops.inspected_paths.size() == 1);
		CHECK(file_ops.read_calls.size() == 1);
	}
}

TEST_CASE("Vegetation chunk set active read contains FileOps runtime errors without a partial snapshot")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-read-fileops-exception");
	const EmptyActiveChunkSetFixture fixture{};
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());

	SUBCASE("InspectPath runtime error")
	{
		file_ops.throw_inspection_call = 1;
	}
	SUBCASE("ReadAllBytes runtime error")
	{
		file_ops.throw_read_call = 1;
	}

	AshEngine::VegetationActiveChunkSetReadResult result{};
	CHECK_NOTHROW(result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops));
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Failed);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read accepts legal empty-error non-success provider shapes")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-provider-status");
	const EmptyActiveChunkSetFixture fixture{};
	fixture.Write(root);
	AshEngine::VegetationFileResultStatus injected =
		AshEngine::VegetationFileResultStatus::NotFound;
	AshEngine::VegetationActiveChunkSetReadStatus expected =
		AshEngine::VegetationActiveChunkSetReadStatus::Invalid;
	bool inject_inspection = true;

	SUBCASE("inspection NotFound maps to Invalid")
	{
		injected = AshEngine::VegetationFileResultStatus::NotFound;
	}
	SUBCASE("inspection LimitExceeded maps to Invalid")
	{
		injected = AshEngine::VegetationFileResultStatus::LimitExceeded;
	}
	SUBCASE("inspection Failed maps to Failed")
	{
		injected = AshEngine::VegetationFileResultStatus::Failed;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
	}
	SUBCASE("bytes NotFound maps to Invalid")
	{
		inject_inspection = false;
		injected = AshEngine::VegetationFileResultStatus::NotFound;
	}
	SUBCASE("bytes LimitExceeded maps to Invalid")
	{
		inject_inspection = false;
		injected = AshEngine::VegetationFileResultStatus::LimitExceeded;
	}
	SUBCASE("bytes Failed maps to Failed")
	{
		inject_inspection = false;
		injected = AshEngine::VegetationFileResultStatus::Failed;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
	}

	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	if (inject_inspection)
	{
		file_ops.overridden_inspection_call = 1;
		file_ops.inspection_override.status = injected;
	}
	else
	{
		file_ops.overridden_read_call = 1;
		file_ops.read_override.status = injected;
	}
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == expected);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read rejects unknown provider status enums as illegal shapes")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-provider-unknown");
	const EmptyActiveChunkSetFixture fixture{};
	fixture.Write(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());

	SUBCASE("unknown inspection status")
	{
		file_ops.overridden_inspection_call = 1;
		file_ops.inspection_override.status =
			static_cast<AshEngine::VegetationFileResultStatus>(0xffu);
		file_ops.inspection_override.error = "injected unknown inspection status";
	}
	SUBCASE("unknown bytes status")
	{
		file_ops.overridden_read_call = 1;
		file_ops.read_override.status =
			static_cast<AshEngine::VegetationFileResultStatus>(0xffu);
		file_ops.read_override.error = "injected unknown bytes status";
	}

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, EmptyChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Failed);
	CHECK_FALSE(result.snapshot);
	CHECK(result.error.find("illegal") != std::string::npos);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read loads one ASVC object with resolved species provenance")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-single-object");
	const SingleObjectActiveChunkSetFixture fixture{};
	REQUIRE(fixture.object_bytes.size() == 284);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
		SingleObjectChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
	REQUIRE(result.snapshot);
	CHECK(result.snapshot->layer_id == fixture.object_chunk.layer_id);
	CHECK(result.snapshot->layer_generation == 7);
	CHECK(result.snapshot->surface_identity.surface_id ==
		fixture.object_chunk.surface_identity.surface_id);
	CHECK(result.snapshot->surface_identity.content_revision == 4);
	CHECK(result.snapshot->surface_identity.residency_revision == 5);
	CHECK(result.snapshot->surface_identity.transform_revision == 6);
	CHECK(result.snapshot->manifest_sha256 == fixture.chunk_set.manifest_sha256);
	REQUIRE(result.snapshot->entries.size() == 1);
	const auto& entry = result.snapshot->entries[0];
	CHECK(entry.coord.x == fixture.object_chunk.chunk.x);
	CHECK(entry.coord.z == fixture.object_chunk.chunk.z);
	CHECK(entry.object_sha256 == AshEngine::vegetation_sha256(
		fixture.object_bytes.data(), fixture.object_bytes.size()));
	CHECK(entry.input_sha256 == fixture.object_chunk.chunk_input_sha256);
	CHECK(entry.referenced_species_ids == std::vector<AshEngine::VegetationId>{
		fixture.object_chunk.species[0].species_id });
	CHECK(result.error.empty());
	CHECK(file_ops.inspected_paths == std::vector<std::filesystem::path>{
		fixture.chunk_set.active_relative_path,
		fixture.chunk_set.manifest_relative_path,
		fixture.object_relative_path });
	REQUIRE(file_ops.read_calls.size() == 3);
	CHECK(file_ops.read_calls[2].path == root.Path() / fixture.object_relative_path);
	CHECK(file_ops.read_calls[2].max_bytes == 284);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read rejects missing corrupt and illegal ASVC storage")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-storage");
	SingleObjectActiveChunkSetFixture fixture{};
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	auto expected = AshEngine::VegetationActiveChunkSetReadStatus::Invalid;
	size_t expected_reads = 3;
	std::string expected_error_fragment{};

	SUBCASE("object is missing")
	{
		fixture.Write(root, false, false);
		expected_reads = 2;
		expected_error_fragment = "object is missing";
	}
	SUBCASE("object bytes mismatch manifest digest")
	{
		fixture.Write(root, false, false);
		auto corrupt = fixture.object_bytes;
		corrupt.back() ^= 0x01u;
		fixture.chunk_set.WriteAsset(root, fixture.object_relative_path, corrupt);
		expected_error_fragment = "digest";
	}
	SUBCASE("hash-consistent object fails strict ASVC codec")
	{
		auto invalid = fixture.object_bytes;
		invalid[152] = 1;
		VegetationTest::RepairChunkHeaderCrc(invalid);
		fixture.SetRawObjectBytes(invalid);
		fixture.Write(root, true, false);
		expected_error_fragment = "invalid";
	}
	SUBCASE("object path is a directory")
	{
		fixture.Write(root, false, false);
		fixture.chunk_set.CreateAssetDirectory(root, fixture.object_relative_path);
		expected_reads = 2;
		expected_error_fragment = "not a regular file";
	}
	SUBCASE("object inspection has an illegal success payload shape")
	{
		fixture.Write(root, true, false);
		file_ops.overridden_inspection_call = 3;
		file_ops.inspection_override.status =
			AshEngine::VegetationFileResultStatus::Succeeded;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
		expected_reads = 2;
		expected_error_fragment = "illegal";
	}
	SUBCASE("object read has an illegal non-success payload shape")
	{
		fixture.Write(root, true, false);
		file_ops.overridden_read_call = 3;
		file_ops.read_override.status = AshEngine::VegetationFileResultStatus::Failed;
		file_ops.read_override.bytes = fixture.object_bytes;
		file_ops.read_override.error = "injected illegal object read payload";
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
		expected_error_fragment = "illegal";
	}

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, SingleObjectChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == expected);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(result.error.find(expected_error_fragment) != std::string::npos);
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == expected_reads);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read validates manifest and ASVC identity fields")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-identity");
	SingleObjectActiveChunkSetFixture fixture{};
	auto mutated = fixture.object_chunk;

	SUBCASE("chunk coord") { ++mutated.chunk.x; }
	SUBCASE("layer id") { mutated.layer_id[0] ^= 0x55u; }
	SUBCASE("surface id") { mutated.surface_identity.surface_id[0] ^= 0x55u; }
	SUBCASE("surface content revision") { ++mutated.surface_identity.content_revision; }
	SUBCASE("surface residency revision") { ++mutated.surface_identity.residency_revision; }
	SUBCASE("surface transform revision") { ++mutated.surface_identity.transform_revision; }
	SUBCASE("chunk input digest") { mutated.chunk_input_sha256[0] ^= 0x55u; }

	fixture.SetObjectChunk(mutated);
	fixture.Write(root, true, false);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, SingleObjectChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK(result.error.find("identities") != std::string::npos);
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == 3);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read validates resolved species contracts")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-species-policy");
	SingleObjectActiveChunkSetFixture fixture{};
	auto mutated = fixture.object_chunk;

	SUBCASE("species path is missing")
	{
		mutated.species[0].species_asset_path = "vegetation/Missing.AshVegetation";
	}
	SUBCASE("resolved species id mismatches palette")
	{
		mutated.species[0].species_id[0] ^= 0x55u;
	}
	SUBCASE("resolved canonical species digest mismatches palette")
	{
		mutated.species[0].species_sha256[0] ^= 0x55u;
	}
	SUBCASE("candidate ordinal equals candidates per cell")
	{
		mutated.instances[0].candidate_ordinal = 8;
	}

	fixture.SetObjectChunk(mutated);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
		SingleObjectChunkSetBudget(),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Invalid);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == 3);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read maps real resolver failures")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-resolver-failure");
	SingleObjectActiveChunkSetFixture fixture{};
	auto budget = SingleObjectChunkSetBudget();
	auto expected = AshEngine::VegetationActiveChunkSetReadStatus::Invalid;
	std::shared_ptr<const AshEngine::VegetationAssetResolverSnapshot> resolver{};
	const std::filesystem::path species_path =
		VegetationTest::ResolvedMinimalPaletteEntry().species_asset_path;

	SUBCASE("Missing maps to Invalid")
	{
		fixture.Write(root, true, false);
	}
	SUBCASE("WrongType maps to Invalid")
	{
		fixture.Write(root, true, false);
		fixture.chunk_set.CreateAssetDirectory(root, species_path);
	}
	SUBCASE("InvalidData maps to Invalid")
	{
		fixture.Write(root, true, false);
		fixture.chunk_set.WriteAsset(root, species_path, { 'x' });
	}
	SUBCASE("BudgetExceeded maps to Invalid")
	{
		fixture.Write(root);
		budget.per_file.max_payload_bytes = 700;
	}
	SUBCASE("Io maps to Failed")
	{
		fixture.Write(root);
		resolver = CaptureFixtureResolver(root);
		std::error_code error{};
		std::filesystem::remove(root.Path() / species_path, error);
		REQUIRE_FALSE(error);
		fixture.chunk_set.CreateAssetDirectory(root, species_path);
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
	}

	if (!resolver)
	{
		resolver = CaptureFixtureResolver(root);
	}
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == expected);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == 3);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read enforces exact composite budgets")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-budget");
	const SingleObjectActiveChunkSetFixture fixture{};
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	auto budget = SingleObjectChunkSetBudget();
	bool succeeds = false;
	size_t expected_inspections = 3;
	size_t expected_reads = 3;
	bool check_third_ceiling = false;

	SUBCASE("exact component manifest total and summary budgets pass") { succeeds = true; }
	SUBCASE("file byte budget one below") { budget.per_file.max_file_bytes = 700; }
	SUBCASE("payload byte budget one below") { budget.per_file.max_payload_bytes = 700; }
	SUBCASE("decoded byte budget one below") { budget.per_file.max_decoded_bytes = 231; }
	SUBCASE("palette record budget one below") { budget.per_file.max_palette_records = 0; }
	SUBCASE("instance record budget one below") { budget.per_file.max_instance_records = 0; }
	SUBCASE("manifest entry budget zero")
	{
		budget.max_manifest_entries = 0;
		expected_inspections = 2;
		expected_reads = 2;
	}
	SUBCASE("total inspected budget one below gives third read ceiling 283")
	{
		budget.max_total_inspected_bytes = 507;
		check_third_ceiling = true;
	}
	SUBCASE("summary budget one below") { budget.max_summary_bytes = 215; }

	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == (succeeds
		? AshEngine::VegetationActiveChunkSetReadStatus::Succeeded
		: AshEngine::VegetationActiveChunkSetReadStatus::Invalid));
	CHECK(static_cast<bool>(result.snapshot) == succeeds);
	CHECK(file_ops.inspected_paths.size() == expected_inspections);
	CHECK(file_ops.read_calls.size() == expected_reads);
	if (check_third_ceiling)
	{
		REQUIRE(file_ops.read_calls.size() == 3);
		CHECK(file_ops.read_calls[2].max_bytes == 283);
	}
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single-object read honors control without partial publication")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-object-control");
	const SingleObjectActiveChunkSetFixture fixture{};
	fixture.Write(root, true, false);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOperationControl control{};
	control.cancel_requested = std::make_shared<std::atomic_bool>(false);
	control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	auto expected = AshEngine::VegetationActiveChunkSetReadStatus::Cancelled;
	size_t expected_calls = 0;

	SUBCASE("cancel after the third successful ASVC read")
	{
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.cancel_after_successful_read = 3;
		expected_calls = 3;
	}
	SUBCASE("pre-cancelled")
	{
		std::const_pointer_cast<std::atomic_bool>(control.cancel_requested)->store(
			true, std::memory_order_release);
	}
	SUBCASE("expired")
	{
		control.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
		expected = AshEngine::VegetationActiveChunkSetReadStatus::TimedOut;
	}

	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path,
		AshEngine::VegetationAssetResolverSnapshot{}, SingleObjectChunkSetBudget(),
		control, file_ops);
	CHECK(result.status == expected);
	CHECK_FALSE(result.snapshot);
	CHECK_FALSE(result.error.empty());
	CHECK(file_ops.inspected_paths.size() == expected_calls);
	CHECK(file_ops.read_calls.size() == expected_calls);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set read loads multiple ASVC objects in manifest order")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-multi-object-order");
	const MultiObjectActiveChunkSetFixture fixture =
		DistinctSpeciesTwoObjectFixture();
	REQUIRE(fixture.object_bytes[0].size() == 284);
	REQUIRE(fixture.object_bytes[1].size() == 408);
	REQUIRE(fixture.TotalInspectedBytes() == 996);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
		ObjectFixtureBudget(fixture, 336),
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);

	CHECK(result.status == AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
	REQUIRE(result.snapshot);
	REQUIRE(result.snapshot->entries.size() == 2);
	const std::vector<std::vector<AshEngine::VegetationId>> expected_species_ids{
		{ fixture.species_assets[0].species.species_id },
		{
			fixture.species_assets[0].species.species_id,
			fixture.species_assets[1].species.species_id
		}
	};
	for (size_t index = 0; index < result.snapshot->entries.size(); ++index)
	{
		const auto& entry = result.snapshot->entries[index];
		CHECK(entry.coord.x == fixture.object_chunks[index].chunk.x);
		CHECK(entry.coord.z == fixture.object_chunks[index].chunk.z);
		CHECK(entry.object_sha256 == AshEngine::vegetation_sha256(
			fixture.object_bytes[index].data(), fixture.object_bytes[index].size()));
		CHECK(entry.input_sha256 == fixture.object_chunks[index].chunk_input_sha256);
		CHECK(entry.referenced_species_ids == expected_species_ids[index]);
		CHECK(std::is_sorted(entry.referenced_species_ids.begin(),
			entry.referenced_species_ids.end()));
	}
	CHECK(file_ops.inspected_paths == std::vector<std::filesystem::path>{
		fixture.chunk_set.active_relative_path,
		fixture.chunk_set.manifest_relative_path,
		fixture.object_relative_paths[0],
		fixture.object_relative_paths[1] });
	REQUIRE(file_ops.read_calls.size() == 4);
	CHECK(file_ops.read_calls[2].path == root.Path() / fixture.object_relative_paths[0]);
	CHECK(file_ops.read_calls[3].path == root.Path() / fixture.object_relative_paths[1]);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set single object charges every referenced Species id")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-dual-species-summary");
	uint16_t second_candidate_ordinal = 7;
	uint64_t max_summary_bytes = 232;
	bool succeeds = false;
	std::string expected_error_fragment{};

	SUBCASE("112 plus 88 plus two Species ids passes at 232 with candidate seven")
	{
		succeeds = true;
	}
	SUBCASE("two Species summary required bytes minus one fails at 231")
	{
		max_summary_bytes = 231;
		expected_error_fragment = "summary";
	}
	SUBCASE("nonzero species index candidate equal to candidates per cell fails")
	{
		second_candidate_ordinal = 8;
		expected_error_fragment = "candidate ordinal";
	}

	const MultiObjectActiveChunkSetFixture fixture =
		DualSpeciesSingleObjectFixture(second_candidate_ordinal);
	REQUIRE(fixture.object_chunks[0].species.size() == 2);
	REQUIRE(fixture.object_chunks[0].instances.size() == 2);
	CHECK(fixture.species_assets[0].species.placement.candidates_per_cell == 1);
	CHECK(fixture.species_assets[1].species.placement.candidates_per_cell == 8);
	CHECK(fixture.object_chunks[0].instances[1].species_index == 1);
	CHECK(fixture.object_chunks[0].instances[1].candidate_ordinal ==
		second_candidate_ordinal);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	const auto budget = ObjectFixtureBudget(fixture, max_summary_bytes);

	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == (succeeds
		? AshEngine::VegetationActiveChunkSetReadStatus::Succeeded
		: AshEngine::VegetationActiveChunkSetReadStatus::Invalid));
	CHECK(static_cast<bool>(result.snapshot) == succeeds);
	if (result.snapshot)
	{
		REQUIRE(result.snapshot->entries.size() == 1);
		CHECK(result.snapshot->entries[0].referenced_species_ids ==
			std::vector<AshEngine::VegetationId>{
				fixture.species_assets[0].species.species_id,
				fixture.species_assets[1].species.species_id });
		CHECK(std::is_sorted(
			result.snapshot->entries[0].referenced_species_ids.begin(),
			result.snapshot->entries[0].referenced_species_ids.end()));
	}
	else if (!succeeds)
	{
		CHECK(result.error.find(expected_error_fragment) != std::string::npos);
	}
	CHECK(file_ops.inspected_paths.size() == 3);
	CHECK(file_ops.read_calls.size() == 3);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set multiple objects charge every per-entry Species reference")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-multi-object-summary");
	const MultiObjectActiveChunkSetFixture fixture =
		DistinctSpeciesTwoObjectFixture();
	REQUIRE(fixture.chunk_set.active_bytes.size() == 48);
	REQUIRE(fixture.chunk_set.manifest_bytes.size() == 256);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	auto budget = ObjectFixtureBudget(fixture, 336);
	bool succeeds = false;

	SUBCASE("A one Species and B two Species pass the exact 336-byte summary")
	{
		succeeds = true;
	}
	SUBCASE("cross-entry repeated Species required bytes minus one fails at 335")
	{
		budget.max_summary_bytes = 335;
	}

	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == (succeeds
		? AshEngine::VegetationActiveChunkSetReadStatus::Succeeded
		: AshEngine::VegetationActiveChunkSetReadStatus::Invalid));
	CHECK(static_cast<bool>(result.snapshot) == succeeds);
	if (result.snapshot)
	{
		REQUIRE(result.snapshot->entries.size() == 2);
		CHECK(result.snapshot->entries[0].referenced_species_ids ==
			std::vector<AshEngine::VegetationId>{
				fixture.species_assets[0].species.species_id });
		CHECK(result.snapshot->entries[1].referenced_species_ids ==
			std::vector<AshEngine::VegetationId>{
				fixture.species_assets[0].species.species_id,
				fixture.species_assets[1].species.species_id });
		CHECK(std::is_sorted(
			result.snapshot->entries[1].referenced_species_ids.begin(),
			result.snapshot->entries[1].referenced_species_ids.end()));
	}
	else if (!succeeds)
	{
		CHECK(result.error.find("summary") != std::string::npos);
	}
	CHECK(file_ops.inspected_paths.size() == 4);
	CHECK(file_ops.read_calls.size() == 4);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set multiple objects keep per-file and cumulative read budgets independent")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-multi-object-read-budget");
	const MultiObjectActiveChunkSetFixture fixture =
		DistinctSpeciesTwoObjectFixture();
	REQUIRE(fixture.chunk_set.active_bytes.size() == 48);
	REQUIRE(fixture.chunk_set.manifest_bytes.size() == 256);
	REQUIRE(fixture.object_bytes[0].size() == 284);
	REQUIRE(fixture.object_bytes[1].size() == 408);
	REQUIRE(fixture.TotalInspectedBytes() == 996);
	fixture.Write(root);
	const auto resolver = CaptureFixtureResolver(root);
	auto budget = ObjectFixtureBudget(fixture, 336);
	bool succeeds = true;
	uint64_t first_object_ceiling = 0;
	uint64_t second_object_ceiling = 0;

	SUBCASE("each object receives the independent per-file ceiling")
	{
		budget.max_total_inspected_bytes = 4096;
		first_object_ceiling = 701;
		second_object_ceiling = 701;
	}
	SUBCASE("exact cumulative total reaches the later object with its exact size")
	{
		first_object_ceiling = 692;
		second_object_ceiling = 408;
	}
	SUBCASE("cumulative total required bytes minus one narrows the later object read")
	{
		budget.max_total_inspected_bytes = 995;
		first_object_ceiling = 691;
		second_object_ceiling = 407;
		succeeds = false;
	}

	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver, budget,
		VegetationTest::ActiveControl(std::chrono::seconds(1)), file_ops);
	CHECK(result.status == (succeeds
		? AshEngine::VegetationActiveChunkSetReadStatus::Succeeded
		: AshEngine::VegetationActiveChunkSetReadStatus::Invalid));
	CHECK(static_cast<bool>(result.snapshot) == succeeds);
	REQUIRE(file_ops.read_calls.size() == 4);
	CHECK(file_ops.read_calls[2].max_bytes == first_object_ceiling);
	CHECK(file_ops.read_calls[3].max_bytes == second_object_ceiling);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set multiple objects never publish a partial snapshot")
{
	VegetationTest::ScopedAssetRoot root("chunk-set-multi-object-no-partial");
	const MultiObjectActiveChunkSetFixture fixture =
		DistinctSpeciesTwoObjectFixture();
	RecordingChunkSetReadFileOps file_ops(AshEngine::get_default_vegetation_file_ops());
	AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(1));
	auto expected = AshEngine::VegetationActiveChunkSetReadStatus::Invalid;
	size_t expected_inspections = 4;
	size_t expected_reads = 4;
	std::string expected_error_fragment{};

	SUBCASE("cancel after the first ASVC object read")
	{
		fixture.Write(root);
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.cancel_after_successful_read = 3;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Cancelled;
		expected_inspections = 3;
		expected_reads = 3;
		expected_error_fragment = "control";
	}
	SUBCASE("cancel after the second ASVC object read")
	{
		fixture.Write(root);
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.cancel_after_successful_read = 4;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Cancelled;
		expected_error_fragment = "control";
	}
	SUBCASE("cancel after the later object inspection with one object accumulated")
	{
		fixture.Write(root);
		file_ops.cancellation = std::const_pointer_cast<std::atomic_bool>(
			control.cancel_requested);
		file_ops.cancel_after_successful_inspection = 4;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Cancelled;
		expected_reads = 3;
		expected_error_fragment = "control";
	}
	SUBCASE("later ASVC object is missing")
	{
		fixture.Write(root, 1, 2);
		expected_reads = 3;
		expected_error_fragment = "missing";
	}
	SUBCASE("later ASVC object digest is corrupt")
	{
		fixture.Write(root);
		auto corrupt = fixture.object_bytes[1];
		corrupt.back() ^= 0x01u;
		fixture.chunk_set.WriteAsset(root,
			fixture.object_relative_paths[1], corrupt);
		expected_error_fragment = "digest";
	}
	SUBCASE("later ASVC object inspection has an illegal provider shape")
	{
		fixture.Write(root);
		file_ops.overridden_inspection_call = 4;
		file_ops.inspection_override.status =
			AshEngine::VegetationFileResultStatus::Succeeded;
		expected = AshEngine::VegetationActiveChunkSetReadStatus::Failed;
		expected_reads = 3;
		expected_error_fragment = "illegal";
	}
	SUBCASE("later ASVC object Species resolution fails")
	{
		fixture.Write(root, 2, 1);
		expected_error_fragment = "not found";
	}

	const auto resolver = CaptureFixtureResolver(root);
	const auto result = AshEngine::read_active_vegetation_chunk_set(
		root.Path(), fixture.chunk_set.layer_relative_path, *resolver,
		ObjectFixtureBudget(fixture, 336), control, file_ops);
	CHECK(result.status == expected);
	CHECK_FALSE(result.snapshot);
	CHECK(result.error.find(expected_error_fragment) != std::string::npos);
	CHECK(file_ops.inspected_paths.size() == expected_inspections);
	CHECK(file_ops.read_calls.size() == expected_reads);
	CHECK(file_ops.mutation_call_count == 0);
}

TEST_CASE("Vegetation chunk set empty ASVM golden encode decode digest")
{
	std::vector<uint8_t> encoded{};
	std::string error = "stale";
	CHECK(AshEngine::encode_vegetation_chunk_set_manifest(
		ManifestFixture(false), encoded, &error));
	CHECK(error.empty());
	CHECK(encoded == EmptyAsvmGolden());
	CHECK(encoded.size() == 96);
	CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(
		encoded.data(), encoded.size())) ==
		"5ab5fd715e95768eb84bfd3494f067d4c4dabec1e3bfac60ae21a3404596e702");

	AshEngine::VegetationChunkSetManifest decoded = ManifestFixture(true);
	error = "stale";
	CHECK(AshEngine::decode_vegetation_chunk_set_manifest(
		EmptyAsvmGolden(), 0, decoded, &error));
	CHECK(error.empty());
	CHECK(decoded.layer_id == VegetationTest::SequentialId(0x00));
	CHECK(decoded.layer_generation == 0x0123456789abcdefull);
	CHECK(decoded.surface_identity.surface_id == VegetationTest::SequentialId(0x10));
	CHECK(decoded.surface_identity.content_revision == 4);
	CHECK(decoded.surface_identity.residency_revision == 5);
	CHECK(decoded.surface_identity.transform_revision == 6);
	CHECK(decoded.entries.empty());
}

TEST_CASE("Vegetation chunk set single ASVM golden encode decode digest")
{
	std::vector<uint8_t> encoded{};
	std::string error = "stale";
	CHECK(AshEngine::encode_vegetation_chunk_set_manifest(
		ManifestFixture(true), encoded, &error));
	CHECK(error.empty());
	CHECK(encoded == SingleAsvmGolden());
	CHECK(encoded.size() == 176);
	CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(
		encoded.data(), encoded.size())) ==
		"f76a5a8bfbc4cb7fcae0f481dae76ef4f4c3f4847c4045d49391f28f47a827ad");

	AshEngine::VegetationChunkSetManifest decoded{};
	CHECK(AshEngine::decode_vegetation_chunk_set_manifest(
		SingleAsvmGolden(), 1, decoded, &error));
	CHECK(error.empty());
	REQUIRE(decoded.entries.size() == 1);
	CHECK(decoded.entries[0].coord.x == -2);
	CHECK(decoded.entries[0].coord.z == 3);
	CHECK(decoded.entries[0].object_sha256 == ManifestFixture(true).entries[0].object_sha256);
	CHECK(decoded.entries[0].input_sha256 == ManifestFixture(true).entries[0].input_sha256);
}

TEST_CASE("Vegetation chunk set ASVA golden encode decode CRC")
{
	AshEngine::VegetationChunkSetActivePointer pointer{};
	pointer.manifest_sha256 = AshEngine::vegetation_sha256(
		SingleAsvmGolden().data(), SingleAsvmGolden().size());
	std::vector<uint8_t> encoded{};
	std::string error = "stale";
	CHECK(AshEngine::encode_vegetation_chunk_set_active_pointer(pointer, encoded, &error));
	CHECK(error.empty());
	CHECK(encoded == AsvaGolden());
	CHECK(encoded.size() == 48);
	CHECK(AshEngine::vegetation_crc32(encoded.data(), 44) == 0x823f6ef3u);

	AshEngine::VegetationChunkSetActivePointer decoded{};
	CHECK(AshEngine::decode_vegetation_chunk_set_active_pointer(
		AsvaGolden(), decoded, &error));
	CHECK(error.empty());
	CHECK(decoded.manifest_sha256 == pointer.manifest_sha256);
}

TEST_CASE("Vegetation chunk set ASVM payload corruption clears output")
{
	std::vector<uint8_t> corrupt = SingleAsvmGolden();
	corrupt.back() ^= 0x01u;
	AshEngine::VegetationChunkSetManifest output = ManifestFixture(true);
	std::string error{};
	CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_manifest(
		corrupt, 1, output, &error));
	CHECK(output.layer_generation == 0);
	CHECK(output.entries.empty());
	CHECK_FALSE(error.empty());
}

TEST_CASE("Vegetation chunk set ASVM tail violates exact EOF and clears output")
{
	std::vector<uint8_t> tailed = EmptyAsvmGolden();
	tailed.push_back(0);
	AshEngine::VegetationChunkSetManifest output = ManifestFixture(true);
	std::string error{};
	CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_manifest(
		tailed, 0, output, &error));
	CHECK(output.layer_generation == 0);
	CHECK(output.entries.empty());
	CHECK_FALSE(error.empty());
}

TEST_CASE("Vegetation chunk set ASVA corruption and tail clear output")
{
	SUBCASE("CRC corruption")
	{
		std::vector<uint8_t> corrupt = AsvaGolden();
		corrupt[8] ^= 0x01u;
		AshEngine::VegetationChunkSetActivePointer output{};
		output.manifest_sha256.fill(0x55);
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_active_pointer(
			corrupt, output, &error));
		CHECK(output.manifest_sha256 == AshEngine::VegetationSha256{});
		CHECK_FALSE(error.empty());
	}

	SUBCASE("exact EOF")
	{
		std::vector<uint8_t> tailed = AsvaGolden();
		tailed.push_back(0);
		AshEngine::VegetationChunkSetActivePointer output{};
		output.manifest_sha256.fill(0x55);
		std::string error{};
		CHECK_FALSE(AshEngine::decode_vegetation_chunk_set_active_pointer(
			tailed, output, &error));
		CHECK(output.manifest_sha256 == AshEngine::VegetationSha256{});
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("Vegetation chunk set encoders reject noncanonical identities ordering and digests")
{
	SUBCASE("strict signed z then x ordering is accepted")
	{
		std::vector<uint8_t> output{};
		std::string error = "stale";
		CHECK(AshEngine::encode_vegetation_chunk_set_manifest(
			TwoEntryManifestFixture(), output, &error));
		CHECK_FALSE(output.empty());
		CHECK(error.empty());
	}

	SUBCASE("reverse ordering is rejected without internal sorting")
	{
		auto manifest = TwoEntryManifestFixture();
		std::reverse(manifest.entries.begin(), manifest.entries.end());
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("duplicate coordinates are rejected")
	{
		auto manifest = TwoEntryManifestFixture();
		manifest.entries[1].coord = manifest.entries[0].coord;
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero layer id is rejected")
	{
		auto manifest = ManifestFixture(true);
		manifest.layer_id = {};
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero layer generation is rejected")
	{
		auto manifest = ManifestFixture(true);
		manifest.layer_generation = 0;
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero surface id is rejected")
	{
		auto manifest = ManifestFixture(true);
		manifest.surface_identity.surface_id = {};
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero object digest is rejected")
	{
		auto manifest = ManifestFixture(true);
		manifest.entries[0].object_sha256 = {};
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero input digest is rejected")
	{
		auto manifest = ManifestFixture(true);
		manifest.entries[0].input_sha256 = {};
		CheckManifestEncodeRejected(manifest);
	}

	SUBCASE("zero ASVA manifest digest is rejected")
	{
		std::vector<uint8_t> output{ 0x55 };
		std::string error{};
		CHECK_FALSE(AshEngine::encode_vegetation_chunk_set_active_pointer(
			{}, output, &error));
		CHECK(output.empty());
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("Vegetation chunk set ASVM decoder rejects strict header identity and digest violations")
{
	SUBCASE("magic")
	{
		auto bytes = EmptyAsvmGolden();
		bytes[0] ^= 0x01u;
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("version")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU16(bytes, 4, 2);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("header size")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU16(bytes, 6, 95);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("reserved")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU32(bytes, 76, 1);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("header CRC")
	{
		auto bytes = EmptyAsvmGolden();
		bytes[92] ^= 0x01u;
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("entry count exceeds admission limit")
	{
		CheckManifestDecodeRejected(SingleAsvmGolden(), 0);
	}

	SUBCASE("entry count and payload bytes disagree")
	{
		auto bytes = SingleAsvmGolden();
		WriteLiteralU32(bytes, 72, 0);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 1);
	}

	SUBCASE("zero layer id")
	{
		auto bytes = EmptyAsvmGolden();
		std::fill_n(bytes.begin() + 8, 16, uint8_t{ 0 });
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("zero layer generation")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU64(bytes, 24, 0);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("zero surface id")
	{
		auto bytes = EmptyAsvmGolden();
		std::fill_n(bytes.begin() + 32, 16, uint8_t{ 0 });
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 0);
	}

	SUBCASE("zero object digest")
	{
		auto bytes = SingleAsvmGolden();
		std::fill_n(bytes.begin() + 112, 32, uint8_t{ 0 });
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 1);
	}

	SUBCASE("zero input digest")
	{
		auto bytes = SingleAsvmGolden();
		std::fill_n(bytes.begin() + 144, 32, uint8_t{ 0 });
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 1);
	}
}

TEST_CASE("Vegetation chunk set ASVM decoder enforces signed ordering and reserve-safe sizing")
{
	SUBCASE("signed x orders entries sharing the same z")
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
			SameZTwoEntryManifestFixture(), bytes, &error));
		AshEngine::VegetationChunkSetManifest decoded{};
		CHECK(AshEngine::decode_vegetation_chunk_set_manifest(
			bytes, 2, decoded, &error));
		REQUIRE(decoded.entries.size() == 2);
		CHECK(decoded.entries[0].coord.z == 4);
		CHECK(decoded.entries[0].coord.x == -1);
		CHECK(decoded.entries[1].coord.z == 4);
		CHECK(decoded.entries[1].coord.x == 1);
	}

	SUBCASE("reverse signed x ordering is rejected for entries sharing the same z")
	{
		auto manifest = SameZTwoEntryManifestFixture();
		std::reverse(manifest.entries.begin(), manifest.entries.end());
		CheckManifestEncodeRejected(manifest);

		std::vector<uint8_t> bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
			SameZTwoEntryManifestFixture(), bytes, &error));
		std::swap_ranges(bytes.begin() + 96, bytes.begin() + 176, bytes.begin() + 176);
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 2);
	}

	SUBCASE("duplicate x is rejected for entries sharing the same z")
	{
		auto manifest = SameZTwoEntryManifestFixture();
		manifest.entries[1].coord = manifest.entries[0].coord;
		CheckManifestEncodeRejected(manifest);

		std::vector<uint8_t> bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
			SameZTwoEntryManifestFixture(), bytes, &error));
		std::copy_n(bytes.begin() + 96, 80, bytes.begin() + 176);
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 2);
	}

	SUBCASE("negative z precedes positive z")
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		CHECK(AshEngine::encode_vegetation_chunk_set_manifest(
			TwoEntryManifestFixture(), bytes, &error));
		AshEngine::VegetationChunkSetManifest decoded{};
		CHECK(AshEngine::decode_vegetation_chunk_set_manifest(
			bytes, 2, decoded, &error));
		REQUIRE(decoded.entries.size() == 2);
		CHECK(decoded.entries[0].coord.z == -1);
		CHECK(decoded.entries[1].coord.z == 2);
	}

	SUBCASE("swapped entries are rejected")
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
			TwoEntryManifestFixture(), bytes, &error));
		std::swap_ranges(bytes.begin() + 96, bytes.begin() + 176, bytes.begin() + 176);
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 2);
	}

	SUBCASE("duplicate entries are rejected")
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		REQUIRE(AshEngine::encode_vegetation_chunk_set_manifest(
			TwoEntryManifestFixture(), bytes, &error));
		std::copy_n(bytes.begin() + 96, 80, bytes.begin() + 176);
		RepairAsvmPayloadAndHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, 2);
	}

	SUBCASE("UINT32 max count with matching payload size is rejected before reserve")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU32(bytes, 72, UINT32_MAX);
		WriteLiteralU64(bytes, 80, static_cast<uint64_t>(UINT32_MAX) * 80u);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, UINT32_MAX);
	}

	SUBCASE("UINT64 max payload size is rejected before reserve")
	{
		auto bytes = EmptyAsvmGolden();
		WriteLiteralU32(bytes, 72, UINT32_MAX);
		WriteLiteralU64(bytes, 80, UINT64_MAX);
		RepairAsvmHeaderCrc(bytes);
		CheckManifestDecodeRejected(bytes, UINT32_MAX);
	}
}

TEST_CASE("Vegetation chunk set ASVA decoder rejects isolated header and identity violations")
{
	SUBCASE("magic")
	{
		auto bytes = AsvaGolden();
		bytes[0] ^= 0x01u;
		RepairAsvaCrc(bytes);
		CheckActiveDecodeRejected(bytes);
	}

	SUBCASE("version")
	{
		auto bytes = AsvaGolden();
		WriteLiteralU16(bytes, 4, 2);
		RepairAsvaCrc(bytes);
		CheckActiveDecodeRejected(bytes);
	}

	SUBCASE("header size")
	{
		auto bytes = AsvaGolden();
		WriteLiteralU16(bytes, 6, 47);
		RepairAsvaCrc(bytes);
		CheckActiveDecodeRejected(bytes);
	}

	SUBCASE("reserved")
	{
		auto bytes = AsvaGolden();
		WriteLiteralU32(bytes, 40, 1);
		RepairAsvaCrc(bytes);
		CheckActiveDecodeRejected(bytes);
	}

	SUBCASE("zero manifest digest")
	{
		auto bytes = AsvaGolden();
		std::fill_n(bytes.begin() + 8, 32, uint8_t{ 0 });
		RepairAsvaCrc(bytes);
		CheckActiveDecodeRejected(bytes);
	}
}
