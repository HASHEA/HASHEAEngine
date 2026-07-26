#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/RenderScene.h"
#include "Function/Asset/TerrainComposition.h"
#include "Function/Asset/TerrainContainer.h"
#include "Function/Render/TerrainLod.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	constexpr uint16_t k_terrain_gate_first_component_x = 12u;
	constexpr uint16_t k_terrain_gate_last_component_x = 20u;
	constexpr uint16_t k_terrain_gate_component_z = 2u;
	constexpr float k_terrain_gate_ring_height = 12.0f;
	constexpr float k_terrain_gate_ring_step = 1.5f;
	constexpr std::array<float, AshEngine::k_terrain_lod_count>
		k_terrain_gate_lod_errors{
			0.0f, 1.0f, 2.5f, 4.0f, 5.5f, 7.0f, 9.0f, 11.5f, 14.0f
		};
	constexpr std::array<uint8_t, 16> k_terrain_gate_weight_layer_id{
		0x41u, 0x73u, 0x68u, 0x54u, 0x65u, 0x72u, 0x72u, 0x61u,
		0x69u, 0x6eu, 0x47u, 0x61u, 0x74u, 0x65u, 0x57u, 0x31u
	};
	constexpr std::string_view k_terrain_gate_generator_environment =
		"ASHENGINE_TERRAIN_GATE_FIXTURE_GENERATOR";
	constexpr std::string_view k_terrain_gate_generator_token =
		"GENERATE_CANONICAL_V1";
	constexpr std::string_view k_terrain_layout_generator_environment =
		"ASHENGINE_TERRAIN_LAYOUT_FIXTURE_GENERATOR";
	constexpr std::string_view k_terrain_layout_generator_token =
		"GENERATE_LAYOUT_MATRIX_V1";

	AshEngine::TerrainReadinessInputs ReadyInputs(uint64_t generation)
	{
		AshEngine::TerrainReadinessInputs inputs{};
		inputs.content_generation = generation;
		inputs.asset_load = AshEngine::TerrainReadinessStage::Ready;
		inputs.asset_load_generation = generation;
		inputs.compose = AshEngine::TerrainReadinessStage::Ready;
		inputs.compose_generation = generation;
		inputs.height_upload = AshEngine::TerrainReadinessStage::Ready;
		inputs.height_upload_generation = generation;
		inputs.atlas_update = AshEngine::TerrainReadinessStage::Ready;
		inputs.atlas_update_generation = generation;
		inputs.scene_packet_succeeded = true;
		return inputs;
	}

	auto Fail(std::string& out_error, std::string message) -> bool
	{
		out_error = std::move(message);
		return false;
	}

	constexpr std::array<uint32_t, 9> k_terrain_gate_oracle_component_min_x{
		3072u, 3328u, 3584u, 3840u, 4096u,
		4352u, 4608u, 4864u, 5120u
	};
	constexpr uint32_t k_terrain_gate_oracle_component_quad_count = 256u;
	constexpr uint32_t k_terrain_gate_oracle_min_x = 3072u;
	constexpr uint32_t k_terrain_gate_oracle_max_x = 5376u;
	constexpr uint32_t k_terrain_gate_oracle_min_z = 512u;
	constexpr uint32_t k_terrain_gate_oracle_max_z = 768u;
	constexpr float k_terrain_gate_oracle_max_height = 12.0f;
	constexpr float k_terrain_gate_oracle_height_step = 1.5f;
	constexpr uint32_t k_terrain_gate_oracle_flat_edge_distance = 8u;

	auto TerrainGateHeightLayoutMatchesIndependentOracle(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		const auto& layout = snapshot.layout;
		if (layout.sample_count_x != 8193u ||
			layout.sample_count_z != 8193u ||
			layout.component_count_x != 32u ||
			layout.component_count_z != 32u ||
			layout.component_quad_count !=
				k_terrain_gate_oracle_component_quad_count ||
			layout.sample_spacing_meters != 1.0f)
		{
			return Fail(out_error,
				"TerrainGate height oracle requires the fixed production layout.");
		}

		const double mapping_min = snapshot.height_mapping.height_offset;
		const double mapping_range = snapshot.height_mapping.height_range;
		const double mapping_max = mapping_min + mapping_range;
		if (!std::isfinite(mapping_min) || !std::isfinite(mapping_range) ||
			!std::isfinite(mapping_max) || mapping_range <= 0.0 ||
			mapping_min > 0.0 || mapping_max < k_terrain_gate_oracle_max_height)
		{
			return Fail(out_error,
				"TerrainGate height oracle requires a finite positive height mapping "
				"whose unclamped interval contains 0..12 m.");
		}

		std::array<float, k_terrain_gate_oracle_flat_edge_distance + 1u>
			expected_heights{};
		std::array<uint16_t, k_terrain_gate_oracle_flat_edge_distance + 1u>
			expected_encoded{};
		for (uint32_t edge_distance = 0u;
			edge_distance <= k_terrain_gate_oracle_flat_edge_distance;
			++edge_distance)
		{
			expected_heights[edge_distance] = std::max(
				0.0f,
				k_terrain_gate_oracle_max_height -
					k_terrain_gate_oracle_height_step *
						static_cast<float>(edge_distance));
			expected_encoded[edge_distance] = AshEngine::encode_terrain_height_r16(
				expected_heights[edge_distance], snapshot.height_mapping);
			if (edge_distance != 0u &&
				expected_encoded[edge_distance - 1u] <=
					expected_encoded[edge_distance])
			{
				return Fail(out_error,
					"TerrainGate height mapping aliases distinct 0..12 m fixture levels.");
			}
		}

		const size_t expected_base_height_count =
			static_cast<size_t>(layout.sample_count_x) * layout.sample_count_z;
		if (!snapshot.base_heights ||
			snapshot.base_heights->size() != expected_base_height_count)
		{
			return Fail(out_error,
				"TerrainGate height oracle requires the complete canonical Base array.");
		}

		for (uint32_t sample_z = 0u; sample_z < layout.sample_count_z; ++sample_z)
		{
			for (uint32_t sample_x = 0u; sample_x < layout.sample_count_x; ++sample_x)
			{
				uint32_t expected_edge_distance =
					k_terrain_gate_oracle_flat_edge_distance;
				const bool inside_design =
					sample_x >= k_terrain_gate_oracle_min_x &&
					sample_x <= k_terrain_gate_oracle_max_x &&
					sample_z >= k_terrain_gate_oracle_min_z &&
					sample_z <= k_terrain_gate_oracle_max_z;
				if (inside_design)
				{
					uint32_t component_index =
						(sample_x - k_terrain_gate_oracle_min_x) /
						k_terrain_gate_oracle_component_quad_count;
					component_index = std::min<uint32_t>(
						component_index,
						static_cast<uint32_t>(
							k_terrain_gate_oracle_component_min_x.size() - 1u));
					const uint32_t local_x =
						sample_x - k_terrain_gate_oracle_component_min_x[component_index];
					const uint32_t local_z = sample_z - k_terrain_gate_oracle_min_z;
					expected_edge_distance = std::min({
						local_x,
						local_z,
						k_terrain_gate_oracle_component_quad_count - local_x,
						k_terrain_gate_oracle_component_quad_count - local_z,
						k_terrain_gate_oracle_flat_edge_distance });
				}
				const size_t sample_index = static_cast<size_t>(sample_z) *
					layout.sample_count_x + sample_x;
				const uint16_t actual_encoded = (*snapshot.base_heights)[sample_index];
				const uint16_t expected_sample_encoded =
					expected_encoded[expected_edge_distance];
				if (actual_encoded == expected_sample_encoded)
				{
					continue;
				}
				std::ostringstream message{};
				message << "TerrainGate height oracle sample (" << sample_x << ", "
					<< sample_z << ") expected "
					<< expected_heights[expected_edge_distance] << " m (R16 "
					<< expected_sample_encoded << ") but loaded R16 "
					<< actual_encoded << '.';
				return Fail(out_error, message.str());
			}
		}
		return true;
	}

	auto MakeAllComponentCoords(const AshEngine::TerrainGridLayout& layout)
		-> std::vector<AshEngine::TerrainComponentCoord>
	{
		std::vector<AshEngine::TerrainComponentCoord> coords{};
		coords.reserve(
			static_cast<size_t>(layout.component_count_x) * layout.component_count_z);
		for (uint32_t z = 0u; z < layout.component_count_z; ++z)
		{
			for (uint32_t x = 0u; x < layout.component_count_x; ++x)
			{
				coords.push_back({
					static_cast<uint16_t>(x), static_cast<uint16_t>(z) });
			}
		}
		return coords;
	}

	auto RecompositionMatchesCanonicalSource(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		AshEngine::TerrainWorkingSet working_set{};
		std::string detail{};
		if (!AshEngine::make_terrain_working_set(snapshot, working_set, &detail))
		{
			return Fail(out_error,
				"TerrainGate canonical working-set construction failed: " + detail);
		}

		const auto requested = MakeAllComponentCoords(snapshot.layout);
		if (snapshot.components.size() != requested.size())
		{
			std::ostringstream message{};
			message << "TerrainGate canonical full composition requested "
				<< requested.size() << " components but loaded "
				<< snapshot.components.size() << '.';
			return Fail(out_error, message.str());
		}

		size_t mismatch_count = 0u;
		std::string first_mismatch{};
		const auto RecordMismatch = [&mismatch_count, &first_mismatch](
			AshEngine::TerrainComponentCoord coord,
			std::string_view field,
			const std::string& mismatch_detail)
		{
			++mismatch_count;
			if (!first_mismatch.empty())
			{
				return;
			}
			std::ostringstream message{};
			message << "component (" << coord.x << ", " << coord.z
				<< ") field " << field;
			if (!mismatch_detail.empty())
			{
				message << ": " << mismatch_detail;
			}
			first_mismatch = message.str();
		};

		constexpr size_t component_batch_size = 16u;
		for (size_t batch_begin = 0u;
			batch_begin < requested.size(); batch_begin += component_batch_size)
		{
			const size_t batch_end = std::min(
				batch_begin + component_batch_size, requested.size());
			const std::vector<AshEngine::TerrainComponentCoord> batch(
				requested.begin() + static_cast<std::ptrdiff_t>(batch_begin),
				requested.begin() + static_cast<std::ptrdiff_t>(batch_end));
			std::vector<AshEngine::TerrainDirtyComponentPayload> recomposed{};
			if (!AshEngine::compose_terrain_components(
					working_set, batch, recomposed, &detail))
			{
				std::ostringstream message{};
				message << "TerrainGate canonical composition batch [" << batch_begin
					<< ", " << batch_end << ") failed: " << detail;
				return Fail(out_error, message.str());
			}
			if (recomposed.size() != batch.size())
			{
				std::ostringstream message{};
				message << "TerrainGate canonical composition batch [" << batch_begin
					<< ", " << batch_end << ") returned " << recomposed.size()
					<< " payloads for " << batch.size() << " requests.";
				return Fail(out_error, message.str());
			}

			for (size_t batch_index = 0u;
				batch_index < recomposed.size(); ++batch_index)
			{
				const size_t index = batch_begin + batch_index;
				const AshEngine::TerrainComponentCoord coord = requested[index];
				const auto& loaded = snapshot.components[index];
				const auto& payload = recomposed[batch_index];
				if (!(payload.coord == coord))
				{
					RecordMismatch(coord, "payload coord", "row-major identity differs");
				}
				if (!loaded || !payload.component)
				{
					RecordMismatch(
						coord, "component", "loaded or recomposed component is null");
					continue;
				}
				const auto& canonical = *payload.component;
				// lod_errors is the intentional exception to recomposition equality:
				// the fixture generator pins automation thresholds after composition,
				// and LodAutomationMetadataMatches owns that separate fixed contract.
				if (!(loaded->coord == coord) || !(canonical.coord == coord))
				{
					RecordMismatch(coord, "component coord", "snapshot identity differs");
				}
				if (loaded->sample_width != canonical.sample_width ||
					loaded->sample_height != canonical.sample_height)
				{
					RecordMismatch(
						coord, "sample dimensions", "loaded and recomposed shapes differ");
				}
				if (loaded->heights != canonical.heights)
				{
					std::ostringstream mismatch{};
					mismatch << "loaded size " << loaded->heights.size()
						<< ", recomposed size " << canonical.heights.size();
					if (loaded->heights.size() == canonical.heights.size())
					{
						const auto first = std::mismatch(
							loaded->heights.begin(), loaded->heights.end(),
							canonical.heights.begin());
						if (first.first != loaded->heights.end())
						{
							const size_t sample_index = static_cast<size_t>(
								std::distance(loaded->heights.begin(), first.first));
							mismatch << ", first sample " << sample_index
								<< " loaded " << *first.first
								<< ", recomposed " << *first.second;
						}
					}
					RecordMismatch(coord, "heights", mismatch.str());
				}
				if (loaded->weights != canonical.weights)
				{
					RecordMismatch(coord, "weights", "loaded and recomposed bytes differ");
				}
				if (loaded->min_max_level_offsets != canonical.min_max_level_offsets)
				{
					RecordMismatch(
						coord, "min/max offsets", "loaded and recomposed offsets differ");
				}
				if (loaded->min_max_levels != canonical.min_max_levels)
				{
					RecordMismatch(
						coord, "min/max levels", "loaded and recomposed ranges differ");
				}
			}
		}

		if (mismatch_count != 0u)
		{
			std::ostringstream message{};
			message << "TerrainGate canonical recomposition found "
				<< mismatch_count << " mismatched component fields; first "
				<< first_mismatch << '.';
			return Fail(out_error, message.str());
		}
		return true;
	}

	auto LodAutomationMetadataMatches(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		size_t mismatch_count = 0u;
		AshEngine::TerrainComponentCoord first_mismatch{};
		for (uint32_t z = 0u; z < snapshot.layout.component_count_z; ++z)
		{
			for (uint32_t x = 0u; x < snapshot.layout.component_count_x; ++x)
			{
				const size_t index = static_cast<size_t>(z) *
					snapshot.layout.component_count_x + x;
				if (index >= snapshot.components.size() ||
					!snapshot.components[index] ||
					snapshot.components[index]->lod_errors != k_terrain_gate_lod_errors)
				{
					if (mismatch_count == 0u)
					{
						first_mismatch = {
							static_cast<uint16_t>(x), static_cast<uint16_t>(z) };
					}
					++mismatch_count;
				}
			}
		}
		if (mismatch_count == 0u)
		{
			return true;
		}
		std::ostringstream message{};
		message << "TerrainGate fixed LOD automation metadata mismatched "
			<< mismatch_count << " components; first (" << first_mismatch.x
			<< ", " << first_mismatch.z << ").";
		return Fail(out_error, message.str());
	}

	auto MaterialRegionsMatch(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		size_t mismatch_count = 0u;
		std::string first_mismatch{};
		const auto CheckRegion = [&](
			AshEngine::TerrainComponentCoord coord,
			const std::array<uint8_t, AshEngine::k_terrain_material_layer_count>& expected)
		{
			const size_t component_index = static_cast<size_t>(coord.z) *
				snapshot.layout.component_count_x + coord.x;
			if (component_index >= snapshot.components.size() ||
				!snapshot.components[component_index])
			{
				++mismatch_count;
				if (first_mismatch.empty())
				{
					first_mismatch = "material component is missing";
				}
				return;
			}
			const auto& component = *snapshot.components[component_index];
			const AshEngine::TerrainSampleRect snapshot_rect =
				AshEngine::get_terrain_component_snapshot_rect(snapshot.layout, coord);
			const AshEngine::TerrainSampleRect owned_rect =
				AshEngine::get_terrain_component_owned_rect(snapshot.layout, coord);
			const size_t component_samples = static_cast<size_t>(component.sample_width) *
				component.sample_height;
			if (snapshot_rect.empty() || owned_rect.empty() ||
				component.sample_width != snapshot_rect.width() ||
				component.sample_height != snapshot_rect.height() ||
				component.weights.size() != component_samples)
			{
				++mismatch_count;
				if (first_mismatch.empty())
				{
					std::ostringstream message{};
					message << "component (" << coord.x << ", " << coord.z
						<< ") does not carry an explicit owned weight region";
					first_mismatch = message.str();
				}
				return;
			}
			for (uint32_t sample_z = owned_rect.min_z;
				sample_z < owned_rect.max_z_exclusive; ++sample_z)
			{
				for (uint32_t sample_x = owned_rect.min_x;
					sample_x < owned_rect.max_x_exclusive; ++sample_x)
				{
					const size_t local_index =
						static_cast<size_t>(sample_z - snapshot_rect.min_z) *
						component.sample_width + (sample_x - snapshot_rect.min_x);
					if (component.weights[local_index] == expected)
					{
						continue;
					}
					++mismatch_count;
					if (first_mismatch.empty())
					{
						std::ostringstream message{};
						message << "component (" << coord.x << ", " << coord.z
							<< ") global sample (" << sample_x << ", "
							<< sample_z << ") weight bytes differ";
						first_mismatch = message.str();
					}
				}
			}
		};

		for (uint32_t layer = 0u;
			layer < AshEngine::k_terrain_material_layer_count; ++layer)
		{
			std::array<uint8_t, AshEngine::k_terrain_material_layer_count> expected{};
			expected[layer] = 255u;
			CheckRegion({
				static_cast<uint16_t>(k_terrain_gate_first_component_x + layer),
				k_terrain_gate_component_z }, expected);
		}
		CheckRegion(
			{ k_terrain_gate_last_component_x, k_terrain_gate_component_z },
			{ 64u, 64u, 64u, 63u, 0u, 0u, 0u, 0u });
		if (mismatch_count == 0u)
		{
			return true;
		}
		std::ostringstream message{};
		message << "TerrainGate material fixture found " << mismatch_count
			<< " mismatched owned samples/regions; first " << first_mismatch << '.';
		return Fail(out_error, message.str());
	}

	auto TerrainGateMaterialFixtureMatchesIndependentOracle(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		if (!snapshot.edit_layers || snapshot.edit_layers->size() != 1u)
		{
			return Fail(out_error,
				"TerrainGate requires exactly one canonical material edit layer.");
		}

		const AshEngine::TerrainEditLayer& layer = snapshot.edit_layers->front();
		if (layer.id.bytes != k_terrain_gate_weight_layer_id ||
			!layer.visible || !std::isfinite(layer.strength) ||
			layer.strength != 1.0f || !layer.height_blocks.empty())
		{
			return Fail(out_error,
				"TerrainGate canonical material layer identity, visibility, strength, "
				"or height-block contract differs.");
		}

		constexpr size_t expected_block_count =
			k_terrain_gate_last_component_x - k_terrain_gate_first_component_x + 1u;
		if (layer.weight_blocks.size() != expected_block_count)
		{
			std::ostringstream message{};
			message << "TerrainGate canonical material layer contains "
				<< layer.weight_blocks.size() << " weight blocks instead of "
				<< expected_block_count << '.';
			return Fail(out_error, message.str());
		}

		const auto ExpectedWeights = [](AshEngine::TerrainComponentCoord coord)
		{
			std::array<uint8_t, AshEngine::k_terrain_material_layer_count> expected{};
			if (coord.z == k_terrain_gate_component_z &&
				coord.x >= k_terrain_gate_first_component_x &&
				coord.x < k_terrain_gate_last_component_x)
			{
				expected[coord.x - k_terrain_gate_first_component_x] = 255u;
			}
			else if (coord.z == k_terrain_gate_component_z &&
				coord.x == k_terrain_gate_last_component_x)
			{
				expected = { 64u, 64u, 64u, 63u, 0u, 0u, 0u, 0u };
			}
			else
			{
				expected[0] = 255u;
			}
			return expected;
		};
		const auto RectsEqual = [](
			const AshEngine::TerrainSampleRect& lhs,
			const AshEngine::TerrainSampleRect& rhs)
		{
			return lhs.min_x == rhs.min_x && lhs.min_z == rhs.min_z &&
				lhs.max_x_exclusive == rhs.max_x_exclusive &&
				lhs.max_z_exclusive == rhs.max_z_exclusive;
		};

		std::array<bool, expected_block_count> seen_blocks{};
		for (const AshEngine::TerrainSparseWeightBlock& block : layer.weight_blocks)
		{
			if (block.owner.z != k_terrain_gate_component_z ||
				block.owner.x < k_terrain_gate_first_component_x ||
				block.owner.x > k_terrain_gate_last_component_x)
			{
				return Fail(out_error,
					"TerrainGate canonical material layer owns an off-design weight block.");
			}
			const size_t design_index =
				block.owner.x - k_terrain_gate_first_component_x;
			if (seen_blocks[design_index])
			{
				return Fail(out_error,
					"TerrainGate canonical material layer contains a duplicate owner.");
			}
			seen_blocks[design_index] = true;
			const AshEngine::TerrainSampleRect expected_rect =
				AshEngine::get_terrain_component_owned_rect(snapshot.layout, block.owner);
			const size_t expected_samples =
				static_cast<size_t>(expected_rect.width()) * expected_rect.height();
			if (expected_rect.empty() || !RectsEqual(block.changed_rect, expected_rect) ||
				block.values.size() != expected_samples ||
				block.coverage.size() != expected_samples)
			{
				return Fail(out_error,
					"TerrainGate canonical material block is not its owner's complete region.");
			}

			const auto expected_bytes = ExpectedWeights(block.owner);
			for (size_t sample_index = 0u;
				sample_index < expected_samples; ++sample_index)
			{
				if (block.coverage[sample_index] != 1.0f)
				{
					return Fail(out_error,
						"TerrainGate canonical material block coverage is not exactly one.");
				}
				for (size_t lane = 0u; lane < expected_bytes.size(); ++lane)
				{
					const float expected =
						static_cast<float>(expected_bytes[lane]) / 255.0f;
					if (block.values[sample_index][lane] != expected)
					{
						return Fail(out_error,
							"TerrainGate canonical material block contains an unexpected value.");
					}
				}
			}
		}
		if (std::find(seen_blocks.begin(), seen_blocks.end(), false) !=
			seen_blocks.end())
		{
			return Fail(out_error,
				"TerrainGate canonical material layer is missing a design owner.");
		}

		const size_t expected_component_count =
			static_cast<size_t>(snapshot.layout.component_count_x) *
			snapshot.layout.component_count_z;
		if (snapshot.components.size() != expected_component_count)
		{
			return Fail(out_error,
				"TerrainGate material oracle requires every resident component.");
		}
		const auto implicit_default_weights = ExpectedWeights({ 0u, 0u });
		for (uint32_t component_z = 0u;
			component_z < snapshot.layout.component_count_z; ++component_z)
		{
			for (uint32_t component_x = 0u;
				component_x < snapshot.layout.component_count_x; ++component_x)
			{
				const AshEngine::TerrainComponentCoord coord{
					static_cast<uint16_t>(component_x),
					static_cast<uint16_t>(component_z) };
				const size_t component_index = static_cast<size_t>(component_z) *
					snapshot.layout.component_count_x + component_x;
				const auto& component = snapshot.components[component_index];
				if (!component || !(component->coord == coord))
				{
					return Fail(out_error,
						"TerrainGate material component row-major identity differs.");
				}
				const AshEngine::TerrainSampleRect snapshot_rect =
					AshEngine::get_terrain_component_snapshot_rect(snapshot.layout, coord);
				const AshEngine::TerrainSampleRect owned_rect =
					AshEngine::get_terrain_component_owned_rect(snapshot.layout, coord);
				const size_t component_samples =
					static_cast<size_t>(component->sample_width) * component->sample_height;
				if (snapshot_rect.empty() || owned_rect.empty() ||
					component->sample_width != snapshot_rect.width() ||
					component->sample_height != snapshot_rect.height() ||
					(!component->weights.empty() &&
						component->weights.size() != component_samples))
				{
					return Fail(out_error,
						"TerrainGate material component weight shape differs.");
				}

				const auto expected = ExpectedWeights(coord);
				for (uint32_t sample_z = owned_rect.min_z;
					sample_z < owned_rect.max_z_exclusive; ++sample_z)
				{
					for (uint32_t sample_x = owned_rect.min_x;
						sample_x < owned_rect.max_x_exclusive; ++sample_x)
					{
						const size_t local_index =
							static_cast<size_t>(sample_z - snapshot_rect.min_z) *
							component->sample_width + (sample_x - snapshot_rect.min_x);
						const auto& actual = component->weights.empty()
							? implicit_default_weights
							: component->weights[local_index];
						if (actual != expected)
						{
							std::ostringstream message{};
							message << "TerrainGate owned material sample (" << sample_x
								<< ", " << sample_z << ") differs from its design/default lane.";
							return Fail(out_error, message.str());
						}
					}
				}
			}
		}
		return true;
	}

	auto TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(
		const AshEngine::TerrainAssetSnapshot& snapshot,
		std::string& out_error) -> bool
	{
		out_error.clear();
		const auto& layout = snapshot.layout;
		if (layout.sample_count_x != 8193u || layout.sample_count_z != 8193u ||
			layout.component_count_x != 32u || layout.component_count_z != 32u ||
			layout.component_quad_count != k_terrain_gate_oracle_component_quad_count ||
			layout.sample_spacing_meters != 1.0f ||
			snapshot.components.size() != 1024u)
		{
			return Fail(out_error,
				"TerrainGate cooked oracle requires the fixed complete production layout.");
		}

		std::array<float, k_terrain_gate_oracle_flat_edge_distance + 1u>
			expected_levels{};
		std::array<uint16_t, k_terrain_gate_oracle_flat_edge_distance + 1u>
			expected_encoded{};
		for (uint32_t edge_distance = 0u;
			edge_distance <= k_terrain_gate_oracle_flat_edge_distance;
			++edge_distance)
		{
			const float analytic_height = std::max(
				0.0f,
				k_terrain_gate_oracle_max_height -
					k_terrain_gate_oracle_height_step * edge_distance);
			expected_encoded[edge_distance] = AshEngine::encode_terrain_height_r16(
				analytic_height, snapshot.height_mapping);
			expected_levels[edge_distance] = AshEngine::decode_terrain_height_r16(
				expected_encoded[edge_distance], snapshot.height_mapping);
			if (!std::isfinite(expected_levels[edge_distance]) ||
				(edge_distance != 0u &&
					expected_encoded[edge_distance - 1u] <=
					expected_encoded[edge_distance]))
			{
				return Fail(out_error,
					"TerrainGate cooked oracle height mapping aliases its analytic levels.");
			}
		}

		const auto ExpectedHeight = [&](uint32_t sample_x, uint32_t sample_z)
		{
			uint32_t edge_distance = k_terrain_gate_oracle_flat_edge_distance;
			if (sample_x >= k_terrain_gate_oracle_min_x &&
				sample_x <= k_terrain_gate_oracle_max_x &&
				sample_z >= k_terrain_gate_oracle_min_z &&
				sample_z <= k_terrain_gate_oracle_max_z)
			{
				const uint32_t component_index = std::min<uint32_t>(
					(sample_x - k_terrain_gate_oracle_min_x) /
						k_terrain_gate_oracle_component_quad_count,
					static_cast<uint32_t>(
						k_terrain_gate_oracle_component_min_x.size() - 1u));
				const uint32_t local_x =
					sample_x - k_terrain_gate_oracle_component_min_x[component_index];
				const uint32_t local_z = sample_z - k_terrain_gate_oracle_min_z;
				edge_distance = std::min({
					local_x,
					local_z,
					k_terrain_gate_oracle_component_quad_count - local_x,
					k_terrain_gate_oracle_component_quad_count - local_z,
					k_terrain_gate_oracle_flat_edge_distance });
			}
			return expected_levels[edge_distance];
		};

		constexpr std::array<uint32_t, 7> hierarchy_widths{
			64u, 32u, 16u, 8u, 4u, 2u, 1u
		};
		constexpr std::array<uint32_t, 10> hierarchy_offsets{
			0u, 4096u, 5120u, 5376u, 5440u,
			5456u, 5460u, 5461u, 5461u, 5461u
		};

		for (uint32_t component_z = 0u;
			component_z < layout.component_count_z; ++component_z)
		{
			for (uint32_t component_x = 0u;
				component_x < layout.component_count_x; ++component_x)
			{
				const AshEngine::TerrainComponentCoord coord{
					static_cast<uint16_t>(component_x),
					static_cast<uint16_t>(component_z) };
				const size_t component_index = static_cast<size_t>(component_z) *
					layout.component_count_x + component_x;
				const auto& component = snapshot.components[component_index];
				const AshEngine::TerrainSampleRect rect =
					AshEngine::get_terrain_component_snapshot_rect(layout, coord);
				if (!component || !(component->coord == coord) ||
					rect.width() != 257u || rect.height() != 257u ||
					component->sample_width != rect.width() ||
					component->sample_height != rect.height() ||
					component->heights.size() != 257u * 257u)
				{
					return Fail(out_error,
						"TerrainGate cooked component identity or height shape differs.");
				}

				std::vector<float> analytic_heights{};
				analytic_heights.reserve(component->heights.size());
				for (uint32_t local_z = 0u; local_z < 257u; ++local_z)
				{
					for (uint32_t local_x = 0u; local_x < 257u; ++local_x)
					{
						const float expected = ExpectedHeight(
							rect.min_x + local_x, rect.min_z + local_z);
						const size_t local_index =
							static_cast<size_t>(local_z) * 257u + local_x;
						if (component->heights[local_index] != expected)
						{
							std::ostringstream message{};
							message << "TerrainGate cooked height at global sample ("
								<< rect.min_x + local_x << ", " << rect.min_z + local_z
								<< ") differs from the analytic fixture.";
							return Fail(out_error, message.str());
						}
						analytic_heights.push_back(expected);
					}
				}

				if (component->min_max_level_offsets != hierarchy_offsets ||
					component->min_max_levels.size() != hierarchy_offsets[7])
				{
					return Fail(out_error,
						"TerrainGate cooked min/max hierarchy shape differs from the fixed contract.");
				}

				std::vector<glm::vec2> analytic_min_max{};
				analytic_min_max.reserve(hierarchy_offsets[7]);
				for (uint32_t block_z = 0u; block_z < hierarchy_widths[0]; ++block_z)
				{
					for (uint32_t block_x = 0u; block_x < hierarchy_widths[0]; ++block_x)
					{
						float minimum = std::numeric_limits<float>::max();
						float maximum = std::numeric_limits<float>::lowest();
						for (uint32_t local_z = block_z * 4u;
							local_z <= block_z * 4u + 4u; ++local_z)
						{
							for (uint32_t local_x = block_x * 4u;
								local_x <= block_x * 4u + 4u; ++local_x)
							{
								const float height = analytic_heights[
									static_cast<size_t>(local_z) * 257u + local_x];
								minimum = std::min(minimum, height);
								maximum = std::max(maximum, height);
							}
						}
						analytic_min_max.push_back({ minimum, maximum });
					}
				}
				for (size_t level = 1u; level < hierarchy_widths.size(); ++level)
				{
					const uint32_t parent_width = hierarchy_widths[level];
					const uint32_t child_width = hierarchy_widths[level - 1u];
					const size_t child_offset = hierarchy_offsets[level - 1u];
					for (uint32_t parent_z = 0u; parent_z < parent_width; ++parent_z)
					{
						for (uint32_t parent_x = 0u; parent_x < parent_width; ++parent_x)
						{
							float minimum = std::numeric_limits<float>::max();
							float maximum = std::numeric_limits<float>::lowest();
							for (uint32_t child_z = parent_z * 2u;
								child_z < parent_z * 2u + 2u; ++child_z)
							{
								for (uint32_t child_x = parent_x * 2u;
									child_x < parent_x * 2u + 2u; ++child_x)
								{
									const glm::vec2 child = analytic_min_max[
										child_offset +
										static_cast<size_t>(child_z) * child_width + child_x];
									minimum = std::min(minimum, child.x);
									maximum = std::max(maximum, child.y);
								}
							}
							analytic_min_max.push_back({ minimum, maximum });
						}
					}
				}
				if (analytic_min_max.size() != component->min_max_levels.size())
				{
					return Fail(out_error,
						"TerrainGate analytic min/max hierarchy size is inconsistent.");
				}
				for (size_t level_index = 0u;
					level_index < analytic_min_max.size(); ++level_index)
				{
					if (component->min_max_levels[level_index] !=
						analytic_min_max[level_index])
					{
						std::ostringstream message{};
						message << "TerrainGate component (" << coord.x << ", " << coord.z
							<< ") min/max entry " << level_index
							<< " differs from the analytic hierarchy.";
						return Fail(out_error, message.str());
					}
				}
				const auto analytic_range = std::minmax_element(
					analytic_heights.begin(), analytic_heights.end());
				if (analytic_range.first == analytic_heights.end() ||
					analytic_min_max.back() !=
						glm::vec2(*analytic_range.first, *analytic_range.second))
				{
					return Fail(out_error,
						"TerrainGate analytic hierarchy root does not bound the component.");
				}
			}
		}
		return true;
	}

	auto ApplyCanonicalRingHeights(
		AshEngine::TerrainWorkingSet& working_set,
		std::string& out_error) -> bool
	{
		out_error.clear();
		const auto& layout = working_set.layout;
		if (layout.sample_count_x != AshEngine::k_terrain_sample_count ||
			layout.sample_count_z != AshEngine::k_terrain_sample_count ||
			layout.component_count_x != AshEngine::k_terrain_component_count ||
			layout.component_count_z != AshEngine::k_terrain_component_count ||
			layout.component_quad_count != AshEngine::k_terrain_component_quad_count)
		{
			return Fail(out_error,
				"TerrainGate generator requires the production Terrain layout.");
		}

		const uint32_t quad_count = layout.component_quad_count;
		const uint32_t design_min_x =
			static_cast<uint32_t>(k_terrain_gate_first_component_x) * quad_count;
		const uint32_t design_min_z =
			static_cast<uint32_t>(k_terrain_gate_component_z) * quad_count;
		const uint32_t design_width =
			(static_cast<uint32_t>(k_terrain_gate_last_component_x) -
				k_terrain_gate_first_component_x + 1u) * quad_count + 1u;
		const uint32_t design_height = quad_count + 1u;
		if (!working_set.base_heights)
		{
			return Fail(out_error, "TerrainGate generator has no Base height array.");
		}
		auto mutable_base_heights =
			std::make_shared<std::vector<uint16_t>>(*working_set.base_heights);
		std::vector<float> designed_heights(
			static_cast<size_t>(design_width) * design_height, 0.0f);
		std::vector<uint8_t> assigned(designed_heights.size(), 0u);

		for (uint32_t component_x = k_terrain_gate_first_component_x;
			component_x <= k_terrain_gate_last_component_x; ++component_x)
		{
			const AshEngine::TerrainComponentCoord coord{
				static_cast<uint16_t>(component_x), k_terrain_gate_component_z };
			const AshEngine::TerrainSampleRect rect =
				AshEngine::get_terrain_component_snapshot_rect(layout, coord);
			if (rect.width() != quad_count + 1u || rect.height() != quad_count + 1u)
			{
				return Fail(out_error,
					"TerrainGate ring design component dimensions are invalid.");
			}
			for (uint32_t local_z = 0u; local_z <= quad_count; ++local_z)
			{
				for (uint32_t local_x = 0u; local_x <= quad_count; ++local_x)
				{
					const uint32_t edge_distance = std::min({
						local_x, local_z, quad_count - local_x, quad_count - local_z });
					const float designed_height = std::max(
						0.0f,
						k_terrain_gate_ring_height -
							k_terrain_gate_ring_step * static_cast<float>(edge_distance));
					const uint32_t sample_x = rect.min_x + local_x;
					const uint32_t sample_z = rect.min_z + local_z;
					const size_t design_index =
						static_cast<size_t>(sample_z - design_min_z) * design_width +
						(sample_x - design_min_x);
					if (design_index >= assigned.size())
					{
						return Fail(out_error,
							"TerrainGate ring design escaped its canonical domain.");
					}
					if (assigned[design_index] != 0u &&
						designed_heights[design_index] != designed_height)
					{
						std::ostringstream message{};
						message << "TerrainGate adjacent ring designs disagree at global sample ("
							<< sample_x << ", " << sample_z << ").";
						return Fail(out_error, message.str());
					}
					assigned[design_index] = 1u;
					designed_heights[design_index] = designed_height;
				}
			}
		}

		for (uint32_t local_z = 0u; local_z < design_height; ++local_z)
		{
			for (uint32_t local_x = 0u; local_x < design_width; ++local_x)
			{
				const size_t design_index =
					static_cast<size_t>(local_z) * design_width + local_x;
				if (assigned[design_index] == 0u)
				{
					return Fail(out_error,
						"TerrainGate ring design left a canonical sample unassigned.");
				}
				const uint32_t sample_x = design_min_x + local_x;
				const uint32_t sample_z = design_min_z + local_z;
				const size_t global_index =
					static_cast<size_t>(sample_z) * layout.sample_count_x + sample_x;
				if (global_index >= mutable_base_heights->size())
				{
					return Fail(out_error,
						"TerrainGate ring design escaped the Base height array.");
				}
				(*mutable_base_heights)[global_index] =
					AshEngine::encode_terrain_height_r16(
						designed_heights[design_index], working_set.height_mapping);
			}
		}
		working_set.base_heights = std::move(mutable_base_heights);
		return true;
	}

	auto BuildCanonicalWeightLayer(
		const AshEngine::TerrainAssetSnapshot& source,
		AshEngine::TerrainEditLayer& out_layer,
		std::string& out_error) -> bool
	{
		out_layer = {};
		out_error.clear();
		out_layer.id.bytes = k_terrain_gate_weight_layer_id;
		out_layer.name = "TerrainGate Material Fixture";
		out_layer.weight_blocks.reserve(
			k_terrain_gate_last_component_x - k_terrain_gate_first_component_x + 1u);
		for (uint32_t component_x = k_terrain_gate_first_component_x;
			component_x <= k_terrain_gate_last_component_x; ++component_x)
		{
			const AshEngine::TerrainComponentCoord coord{
				static_cast<uint16_t>(component_x), k_terrain_gate_component_z };
			const size_t component_index = static_cast<size_t>(coord.z) *
				source.layout.component_count_x + coord.x;
			if (component_index >= source.components.size() ||
				!source.components[component_index])
			{
				return Fail(out_error,
					"TerrainGate source material component is missing.");
			}
			const auto& component = *source.components[component_index];
			const AshEngine::TerrainSampleRect snapshot_rect =
				AshEngine::get_terrain_component_snapshot_rect(source.layout, coord);
			const AshEngine::TerrainSampleRect owned_rect =
				AshEngine::get_terrain_component_owned_rect(source.layout, coord);
			const size_t component_samples = static_cast<size_t>(component.sample_width) *
				component.sample_height;
			if (snapshot_rect.empty() || owned_rect.empty() ||
				component.sample_width != snapshot_rect.width() ||
				component.sample_height != snapshot_rect.height() ||
				component.weights.size() != component_samples)
			{
				return Fail(out_error,
					"TerrainGate source material component weights are invalid.");
			}

			AshEngine::TerrainSparseWeightBlock block{};
			block.owner = coord;
			block.changed_rect = owned_rect;
			const size_t block_samples = static_cast<size_t>(owned_rect.width()) *
				owned_rect.height();
			block.values.resize(block_samples);
			block.coverage.assign(block_samples, 1.0f);
			for (uint32_t sample_z = owned_rect.min_z;
				sample_z < owned_rect.max_z_exclusive; ++sample_z)
			{
				for (uint32_t sample_x = owned_rect.min_x;
					sample_x < owned_rect.max_x_exclusive; ++sample_x)
				{
					const size_t component_sample =
						static_cast<size_t>(sample_z - snapshot_rect.min_z) *
						component.sample_width + (sample_x - snapshot_rect.min_x);
					const size_t block_sample =
						static_cast<size_t>(sample_z - owned_rect.min_z) *
						owned_rect.width() + (sample_x - owned_rect.min_x);
					for (size_t lane = 0u;
						lane < AshEngine::k_terrain_material_layer_count; ++lane)
					{
						block.values[block_sample][lane] =
							static_cast<float>(component.weights[component_sample][lane]) /
							255.0f;
					}
				}
			}
			out_layer.weight_blocks.push_back(std::move(block));
		}
		return true;
	}

	auto HasExactGeneratorOptIn() -> bool
	{
		char* value = nullptr;
		size_t value_size = 0u;
		const auto result = _dupenv_s(
			&value, &value_size, k_terrain_gate_generator_environment.data());
		std::unique_ptr<char, decltype(&std::free)> owned_value(value, &std::free);
		if (result != 0 || value == nullptr)
		{
			return false;
		}
		const std::string_view actual(value);
		return actual == k_terrain_gate_generator_token &&
			value_size == actual.size() + 1u;
	}

	auto HasExactLayoutGeneratorOptIn() -> bool
	{
		char* value = nullptr;
		size_t value_size = 0u;
		const auto result = _dupenv_s(
			&value, &value_size, k_terrain_layout_generator_environment.data());
		std::unique_ptr<char, decltype(&std::free)> owned_value(value, &std::free);
		if (result != 0 || value == nullptr)
		{
			return false;
		}
		const std::string_view actual(value);
		return actual == k_terrain_layout_generator_token &&
			value_size == actual.size() + 1u;
	}

	struct TerrainLayoutFixtureCase
	{
		std::string_view name{};
		uint32_t extent_x_meters = 0u;
		uint32_t extent_z_meters = 0u;
	};

	auto WriteTerrainLayoutScene(
		const std::filesystem::path& path,
		const TerrainLayoutFixtureCase& fixture) -> bool
	{
		const uint32_t camera_height =
			std::clamp(fixture.extent_z_meters / 8u, 100u, 300u);
		const uint32_t camera_back =
			std::clamp(fixture.extent_z_meters / 4u, 250u, 500u);
		const uint32_t far_plane =
			std::max(12000u, fixture.extent_z_meters + 2000u);
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.is_open())
		{
			return false;
		}
		output
			<< "{\n"
			<< "  \"entities\": [\n"
			<< "    {\n"
			<< "      \"id\": 1,\n"
			<< "      \"name\": \"TerrainLayoutRoot\",\n"
			<< "      \"parent\": 0,\n"
			<< "      \"transform\": {\n"
			<< "        \"position\": [0.0, 0.0, 0.0],\n"
			<< "        \"rotation_euler_degrees\": [0.0, 0.0, 0.0],\n"
			<< "        \"scale\": [1.0, 1.0, 1.0]\n"
			<< "      }\n"
			<< "    },\n"
			<< "    {\n"
			<< "      \"camera\": {\n"
			<< "        \"far_plane\": " << far_plane << ".0,\n"
			<< "        \"fov_y_degrees\": 60.0,\n"
			<< "        \"near_plane\": 0.1,\n"
			<< "        \"orthographic_height\": 10.0,\n"
			<< "        \"primary\": true,\n"
			<< "        \"projection\": 0,\n"
			<< "        \"reverse_z\": true\n"
			<< "      },\n"
			<< "      \"id\": 2,\n"
			<< "      \"name\": \"TerrainLayoutCamera\",\n"
			<< "      \"parent\": 1,\n"
			<< "      \"transform\": {\n"
			<< "        \"position\": [" << fixture.extent_x_meters / 2u
			<< ".0, " << camera_height << ".0, -" << camera_back << ".0],\n"
			<< "        \"rotation_euler_degrees\": [16.5, 0.0, 0.0],\n"
			<< "        \"scale\": [1.0, 1.0, 1.0]\n"
			<< "      }\n"
			<< "    },\n"
			<< "    {\n"
			<< "      \"id\": 3,\n"
			<< "      \"light\": {\n"
			<< "        \"casts_shadow\": true,\n"
			<< "        \"color\": [1.0, 0.95, 0.88],\n"
			<< "        \"inner_cone_angle_degrees\": 30.0,\n"
			<< "        \"intensity\": 2.5,\n"
			<< "        \"near_shadow_distance\": 64.0,\n"
			<< "        \"outer_cone_angle_degrees\": 45.0,\n"
			<< "        \"range\": 10.0,\n"
			<< "        \"shadow_cascade_count\": 4,\n"
			<< "        \"shadow_distance\": 5000.0,\n"
			<< "        \"shadow_priority\": 255,\n"
			<< "        \"sunlight\": true,\n"
			<< "        \"type\": 0\n"
			<< "      },\n"
			<< "      \"name\": \"TerrainLayoutSunlight\",\n"
			<< "      \"parent\": 1,\n"
			<< "      \"transform\": {\n"
			<< "        \"position\": [0.0, 0.0, 0.0],\n"
			<< "        \"rotation_euler_degrees\": [45.0, -30.0, 0.0],\n"
			<< "        \"scale\": [1.0, 1.0, 1.0]\n"
			<< "      }\n"
			<< "    },\n"
			<< "    {\n"
			<< "      \"id\": 4,\n"
			<< "      \"name\": \"TerrainLayout\",\n"
			<< "      \"parent\": 1,\n"
			<< "      \"terrain\": {\n"
			<< "        \"asset_path\": \"terrain/generated-layout-fixtures/"
			<< fixture.name << "/TerrainLayout.AshTerrain\",\n"
			<< "        \"casts_shadow\": true,\n"
			<< "        \"material_layer_overrides\": [\"\", \"\", \"\", \"\", "
				"\"\", \"\", \"\", \"\"],\n"
			<< "        \"receives_shadow\": true,\n"
			<< "        \"visible\": true\n"
			<< "      },\n"
			<< "      \"transform\": {\n"
			<< "        \"position\": [0.0, 0.0, 0.0],\n"
			<< "        \"rotation_euler_degrees\": [0.0, 0.0, 0.0],\n"
			<< "        \"scale\": [1.0, 1.0, 1.0]\n"
			<< "      }\n"
			<< "    }\n"
			<< "  ],\n"
			<< "  \"name\": \"TerrainLayout-" << fixture.name << "\",\n"
			<< "  \"next_entity_id\": 5,\n"
			<< "  \"scene_config\": {\n"
			<< "    \"ambient_occlusion\": { \"mode\": \"Off\" },\n"
			<< "    \"bloom\": { \"enabled\": false },\n"
			<< "    \"directional_shadows\": {\n"
			<< "      \"default_cascade_count\": 4,\n"
			<< "      \"default_shadow_distance\": 5000.0,\n"
			<< "      \"enabled\": true,\n"
			<< "      \"near_shadow_distance\": 64.0\n"
			<< "    },\n"
			<< "    \"temporal_aa\": { \"enabled\": false },\n"
			<< "    \"tonemap\": { \"exposure\": 1.0 },\n"
			<< "    \"volumetric_lighting\": { \"enabled\": false }\n"
			<< "  },\n"
			<< "  \"version\": 6\n"
			<< "}\n";
		return output.good();
	}

	struct ScopedGeneratedFixtureDirectory
	{
		std::filesystem::path path{};
		bool committed = false;

		~ScopedGeneratedFixtureDirectory()
		{
			if (!committed)
			{
				std::error_code ignored{};
				std::filesystem::remove_all(path, ignored);
			}
		}
	};

	struct ScopedStagingCleanup
	{
		std::filesystem::path file{};
		std::filesystem::path directory{};

		~ScopedStagingCleanup()
		{
			std::error_code ignored{};
			std::filesystem::remove(file, ignored);
			ignored.clear();
			std::filesystem::remove(directory, ignored);
		}
	};
}

TEST_CASE("Terrain readiness waits for compose upload atlas and scene submit")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(7u);
	inputs.atlas_update = AshEngine::TerrainReadinessStage::Pending;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);

	inputs.atlas_update = AshEngine::TerrainReadinessStage::Ready;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Ready);

	inputs.scene_packet_succeeded = false;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);
}

TEST_CASE("Terrain readiness gives current generation failure precedence")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(11u);
	inputs.compose = AshEngine::TerrainReadinessStage::Pending;
	inputs.height_upload = AshEngine::TerrainReadinessStage::Failed;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Failed);
}

TEST_CASE("Terrain readiness rejects stale generation checkpoints")
{
	AshEngine::TerrainReadinessInputs inputs = ReadyInputs(13u);
	inputs.height_upload_generation = 12u;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);

	inputs.height_upload = AshEngine::TerrainReadinessStage::Failed;
	CHECK(AshEngine::evaluate_terrain_readiness(inputs) ==
		AshEngine::TerrainReadinessStage::Pending);
}

TEST_CASE("Terrain scene resolve status remains fail closed for automation")
{
	CHECK(AshEngine::evaluate_terrain_scene_resolve_readiness(
		AshEngine::TerrainSceneResolveStatus::Ready) ==
		AshEngine::TerrainReadinessStage::Ready);
	CHECK(AshEngine::evaluate_terrain_scene_resolve_readiness(
		AshEngine::TerrainSceneResolveStatus::Pending) ==
		AshEngine::TerrainReadinessStage::Pending);
	CHECK(AshEngine::evaluate_terrain_scene_resolve_readiness(
		AshEngine::TerrainSceneResolveStatus::Failed) ==
		AshEngine::TerrainReadinessStage::Failed);
}

TEST_CASE("Terrain readiness fixture loads all LOD and material regions")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	AshEngine::TerrainContainerLoadReport report{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		"product/assets/terrain/TerrainGate.AshTerrain",
		snapshot,
		&report,
		&error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(snapshot != nullptr);
	CHECK(report.loaded_generation == 1u);
	CHECK(snapshot->layout.sample_count_x == 8193u);
	CHECK(snapshot->layout.sample_count_z == 8193u);
	REQUIRE(snapshot->components.size() == 1024u);
	REQUIRE(snapshot->base_heights != nullptr);
	REQUIRE(snapshot->edit_layers != nullptr);
	const bool height_layout_matches =
		TerrainGateHeightLayoutMatchesIndependentOracle(*snapshot, error);
	CHECK_MESSAGE(height_layout_matches, error);
	for (const AshEngine::TerrainEditLayer& layer : *snapshot->edit_layers)
	{
		CHECK_MESSAGE(
			layer.height_blocks.empty(),
			"TerrainGate keeps its deterministic height fixture in canonical Base data.");
	}

	const bool canonical_matches =
		RecompositionMatchesCanonicalSource(*snapshot, error);
	CHECK_MESSAGE(canonical_matches, error);
	const bool lod_metadata_matches = LodAutomationMetadataMatches(*snapshot, error);
	CHECK_MESSAGE(lod_metadata_matches, error);
	const bool material_regions_match = MaterialRegionsMatch(*snapshot, error);
	CHECK_MESSAGE(material_regions_match, error);
	const bool material_fixture_matches =
		TerrainGateMaterialFixtureMatchesIndependentOracle(*snapshot, error);
	CHECK_MESSAGE(material_fixture_matches, error);
	const bool cooked_height_and_min_max_match =
		TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(*snapshot, error);
	CHECK_MESSAGE(cooked_height_and_min_max_match, error);

	size_t non_flat_component_count = 0u;
	for (const auto& component : snapshot->components)
	{
		REQUIRE(component != nullptr);
		REQUIRE_FALSE(component->min_max_levels.empty());
		const glm::vec2 root_range = component->min_max_levels.back();
		if (root_range.y - root_range.x > 0.0001f)
		{
			++non_flat_component_count;
		}
	}
	CHECK(non_flat_component_count > 0u);

	AshEngine::Scene scene = AshEngine::Scene::load_from_file(
		"product/assets/scenes/Terrain.scene.json", &error);
	REQUIRE_MESSAGE(scene.is_valid(), error);
	REQUIRE(scene.get_entities_with_component(
		AshEngine::SceneComponentType::Terrain).size() == 1u);
	AshEngine::SceneView view{};
	REQUIRE(AshEngine::build_primary_scene_view(
		scene, { 2560u, 1440u }, view));
	AshEngine::TerrainLodInput lod_input{};
	lod_input.asset_snapshot = snapshot;
	lod_input.view = view;
	AshEngine::TerrainLodResult lod_result{};
	REQUIRE(AshEngine::build_terrain_lod_batches(lod_input, lod_result));
	CHECK(lod_result.batches.size() == AshEngine::k_terrain_lod_count);
}

TEST_CASE("TerrainGate height oracle rejects canonical Base mutations")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		"product/assets/terrain/TerrainGate.AshTerrain",
		snapshot,
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(snapshot != nullptr);
	REQUIRE(snapshot->base_heights != nullptr);
	const bool original_matches =
		TerrainGateHeightLayoutMatchesIndependentOracle(*snapshot, error);
	REQUIRE_MESSAGE(original_matches, error);

	AshEngine::TerrainAssetSnapshot mutated = *snapshot;
	auto mutable_base =
		std::make_shared<std::vector<uint16_t>>(*snapshot->base_heights);
	mutated.base_heights = mutable_base;
	const auto SampleIndex = [&mutated](uint32_t sample_x, uint32_t sample_z)
	{
		return static_cast<size_t>(sample_z) * mutated.layout.sample_count_x +
			sample_x;
	};

	const size_t interior_index = SampleIndex(3077u, 518u);
	const uint16_t interior_original = (*mutable_base)[interior_index];
	REQUIRE(interior_original != 0u);
	(*mutable_base)[interior_index] = 0u;
	CHECK_FALSE(TerrainGateHeightLayoutMatchesIndependentOracle(mutated, error));
	(*mutable_base)[interior_index] = interior_original;

	const size_t shared_edge_index = SampleIndex(3328u, 576u);
	const uint16_t shared_edge_original = (*mutable_base)[shared_edge_index];
	REQUIRE(shared_edge_original != 0u);
	(*mutable_base)[shared_edge_index] = 0u;
	CHECK_FALSE(TerrainGateHeightLayoutMatchesIndependentOracle(mutated, error));
	(*mutable_base)[shared_edge_index] = shared_edge_original;

	std::fill(mutable_base->begin(), mutable_base->end(), 0u);
	mutated.height_mapping.height_offset = 12.0f;
	mutated.height_mapping.height_range = 1.0f;
	CHECK_FALSE(TerrainGateHeightLayoutMatchesIndependentOracle(mutated, error));
}

TEST_CASE("TerrainGate material oracle rejects extra layers and off-design weights")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		"product/assets/terrain/TerrainGate.AshTerrain",
		snapshot,
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(snapshot != nullptr);
	REQUIRE(snapshot->edit_layers != nullptr);
	REQUIRE(TerrainGateMaterialFixtureMatchesIndependentOracle(*snapshot, error));

	AshEngine::TerrainAssetSnapshot extra_layer = *snapshot;
	auto extra_layers =
		std::make_shared<std::vector<AshEngine::TerrainEditLayer>>(*snapshot->edit_layers);
	AshEngine::TerrainEditLayer unexpected_layer{};
	unexpected_layer.id.bytes[0] = 0xffu;
	unexpected_layer.name = "Unexpected TerrainGate layer";
	extra_layers->push_back(std::move(unexpected_layer));
	extra_layer.edit_layers = extra_layers;
	CHECK_FALSE(TerrainGateMaterialFixtureMatchesIndependentOracle(extra_layer, error));

	AshEngine::TerrainAssetSnapshot polluted = *snapshot;
	auto polluted_component =
		std::make_shared<AshEngine::TerrainComponentSnapshot>(*snapshot->components.front());
	const size_t component_sample_count =
		static_cast<size_t>(polluted_component->sample_width) *
		polluted_component->sample_height;
	std::array<uint8_t, AshEngine::k_terrain_material_layer_count> default_weights{};
	default_weights[0] = 255u;
	polluted_component->weights.assign(component_sample_count, default_weights);
	const AshEngine::TerrainSampleRect snapshot_rect =
		AshEngine::get_terrain_component_snapshot_rect(snapshot->layout, { 0u, 0u });
	const AshEngine::TerrainSampleRect owned_rect =
		AshEngine::get_terrain_component_owned_rect(snapshot->layout, { 0u, 0u });
	std::array<uint8_t, AshEngine::k_terrain_material_layer_count> pollution{};
	pollution[1] = 255u;
	for (uint32_t sample_z = owned_rect.min_z;
		sample_z < owned_rect.max_z_exclusive; ++sample_z)
	{
		for (uint32_t sample_x = owned_rect.min_x;
			sample_x < owned_rect.max_x_exclusive; ++sample_x)
		{
			const size_t local_index =
				static_cast<size_t>(sample_z - snapshot_rect.min_z) *
				polluted_component->sample_width +
				(sample_x - snapshot_rect.min_x);
			polluted_component->weights[local_index] = pollution;
		}
	}
	polluted.components.front() = polluted_component;
	CHECK_FALSE(TerrainGateMaterialFixtureMatchesIndependentOracle(polluted, error));
}

TEST_CASE("TerrainGate cooked oracle rejects height and min max mutations")
{
	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		"product/assets/terrain/TerrainGate.AshTerrain",
		snapshot,
		nullptr,
		&error) == AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(snapshot != nullptr);
	REQUIRE(TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(
		*snapshot, error));

	const size_t shaped_component_index =
		static_cast<size_t>(k_terrain_gate_component_z) *
			snapshot->layout.component_count_x +
		k_terrain_gate_first_component_x;
	REQUIRE(shaped_component_index < snapshot->components.size());
	REQUIRE(snapshot->components[shaped_component_index] != nullptr);

	AshEngine::TerrainAssetSnapshot height_mutation = *snapshot;
	auto changed_height = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*snapshot->components[shaped_component_index]);
	REQUIRE(changed_height->heights.size() > 5u);
	changed_height->heights[5u] += 0.25f;
	height_mutation.components[shaped_component_index] = changed_height;
	CHECK_FALSE(TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(
		height_mutation, error));

	AshEngine::TerrainAssetSnapshot leaf_mutation = *snapshot;
	auto changed_leaf = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*snapshot->components[shaped_component_index]);
	REQUIRE_FALSE(changed_leaf->min_max_levels.empty());
	changed_leaf->min_max_levels.front().x -= 0.25f;
	leaf_mutation.components[shaped_component_index] = changed_leaf;
	CHECK_FALSE(TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(
		leaf_mutation, error));

	AshEngine::TerrainAssetSnapshot root_mutation = *snapshot;
	auto changed_root = std::make_shared<AshEngine::TerrainComponentSnapshot>(
		*snapshot->components[shaped_component_index]);
	REQUIRE_FALSE(changed_root->min_max_levels.empty());
	changed_root->min_max_levels.back().y += 0.25f;
	root_mutation.components[shaped_component_index] = changed_root;
	CHECK_FALSE(TerrainGateCookedHeightAndMinMaxMatchIndependentOracle(
		root_mutation, error));
}

TEST_CASE("TerrainGate full pressure layout is independent of the authoring default")
{
	const AshEngine::TerrainGridLayout full_pressure_layout{
		8193u, 8193u, 32u, 32u, 256u, 1.0f
	};
	REQUIRE(AshEngine::is_valid_terrain_grid_layout(full_pressure_layout));
	const AshEngine::TerrainGridLayout authoring_default =
		AshEngine::make_terrain_authoring_grid_layout(2048u, 2048u);
	CHECK(full_pressure_layout.sample_count_x == 8193u);
	CHECK(full_pressure_layout.sample_count_z == 8193u);
	CHECK(full_pressure_layout.component_count_x == 32u);
	CHECK(full_pressure_layout.component_count_z == 32u);
	CHECK(authoring_default.sample_count_x == 2049u);
	CHECK(authoring_default.sample_count_z == 2049u);
	CHECK(authoring_default.component_count_x == 8u);
	CHECK(authoring_default.component_count_z == 8u);
}

TEST_CASE("Terrain layout runtime fixture generator emits isolated assets" *
	doctest::skip())
{
	REQUIRE_MESSAGE(
		HasExactLayoutGeneratorOptIn(),
		"Generator is disabled. Explicitly run this skipped test with "
		"ASHENGINE_TERRAIN_LAYOUT_FIXTURE_GENERATOR=GENERATE_LAYOUT_MATRIX_V1 "
		"and --no-skip=true.");
	const std::filesystem::path output_root =
		"Intermediate/generated-fixtures/terrain-layouts";
	std::error_code filesystem_error{};
	const bool output_exists =
		std::filesystem::exists(output_root, filesystem_error);
	REQUIRE_MESSAGE(
		!filesystem_error,
		"Could not inspect the Terrain layout generated-fixture directory.");
	REQUIRE_MESSAGE(
		!output_exists,
		"Refusing to replace the existing Terrain layout generated-fixture directory.");
	const bool output_created =
		std::filesystem::create_directories(output_root, filesystem_error);
	const bool output_ready = !filesystem_error && output_created;
	REQUIRE_MESSAGE(
		output_ready,
		"Could not create the Terrain layout generated-fixture directory.");
	ScopedGeneratedFixtureDirectory cleanup{ output_root };

	constexpr std::array<TerrainLayoutFixtureCase, 3> fixtures = {
		TerrainLayoutFixtureCase{ "min", 256u, 256u },
		TerrainLayoutFixtureCase{ "rect", 2048u, 4096u },
		TerrainLayoutFixtureCase{ "default", 2048u, 2048u }
	};
	for (size_t index = 0u; index < fixtures.size(); ++index)
	{
		const TerrainLayoutFixtureCase& fixture = fixtures[index];
		const std::filesystem::path fixture_directory =
			output_root / fixture.name;
		filesystem_error.clear();
		const bool fixture_directory_created =
			std::filesystem::create_directory(fixture_directory, filesystem_error);
		const bool fixture_directory_ready =
			!filesystem_error && fixture_directory_created;
		REQUIRE_MESSAGE(
			fixture_directory_ready,
			"Could not create an isolated Terrain layout fixture directory.");

		const AshEngine::TerrainGridLayout layout =
			AshEngine::make_terrain_authoring_grid_layout(
				fixture.extent_x_meters, fixture.extent_z_meters);
		REQUIRE(layout.sample_count_x == fixture.extent_x_meters + 1u);
		REQUIRE(layout.sample_count_z == fixture.extent_z_meters + 1u);
		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> snapshot{};
		std::string error{};
		REQUIRE(AshEngine::create_flat_terrain_snapshot(
			static_cast<AshEngine::TerrainAssetId>(1001u + index),
			layout,
			{ -100.0f, 1000.0f },
			0.0f,
			snapshot,
			&error));
		REQUIRE_MESSAGE(error.empty(), error);
		REQUIRE(snapshot != nullptr);
		const std::filesystem::path asset_path =
			fixture_directory / "TerrainLayout.AshTerrain";
		AshEngine::TerrainContainerSaveReport save_report{};
		REQUIRE(AshEngine::save_terrain_container_incremental(
			asset_path, *snapshot, {}, &save_report, &error) ==
			AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
		snapshot.reset();

		std::shared_ptr<const AshEngine::TerrainAssetSnapshot> round_trip{};
		AshEngine::TerrainContainerLoadReport load_report{};
		REQUIRE(AshEngine::load_terrain_container(
			asset_path, round_trip, &load_report, &error) ==
			AshEngine::TerrainContainerResult::Success);
		REQUIRE_MESSAGE(error.empty(), error);
		REQUIRE(round_trip != nullptr);
		CHECK(round_trip->layout.sample_count_x == layout.sample_count_x);
		CHECK(round_trip->layout.sample_count_z == layout.sample_count_z);
		CHECK(round_trip->layout.component_count_x == layout.component_count_x);
		CHECK(round_trip->layout.component_count_z == layout.component_count_z);
		round_trip.reset();

		const std::filesystem::path scene_path =
			fixture_directory / "Terrain.scene.json";
		REQUIRE(WriteTerrainLayoutScene(scene_path, fixture));
		std::ifstream scene_input(scene_path, std::ios::binary);
		REQUIRE(scene_input.is_open());
		const std::string scene_text{
			std::istreambuf_iterator<char>(scene_input),
			std::istreambuf_iterator<char>() };
		CHECK(scene_text.find("\"version\": 6") != std::string::npos);
		CHECK(scene_text.find(
			"terrain/generated-layout-fixtures/" + std::string(fixture.name) +
				"/TerrainLayout.AshTerrain") != std::string::npos);
	}
	cleanup.committed = true;
}

TEST_CASE("TerrainGate canonical fixture generator emits a staged candidate" *
	doctest::skip())
{
	REQUIRE_MESSAGE(
		HasExactGeneratorOptIn(),
		"Generator is disabled. Explicitly run this skipped test with "
		"ASHENGINE_TERRAIN_GATE_FIXTURE_GENERATOR=GENERATE_CANONICAL_V1 "
		"and --no-skip=true.");

	const std::filesystem::path source_path =
		"product/assets/terrain/TerrainGate.AshTerrain";
	const std::filesystem::path output_path =
		"Intermediate/generated-fixtures/TerrainGate.AshTerrain";
	std::error_code filesystem_error{};
	const bool output_exists = std::filesystem::exists(output_path, filesystem_error);
	REQUIRE_MESSAGE(
		!filesystem_error,
		"Could not inspect the fixed TerrainGate generated-fixture path.");
	REQUIRE_MESSAGE(
		!output_exists,
		"Refusing to replace the existing staged TerrainGate generated fixture.");

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> source{};
	AshEngine::TerrainContainerLoadReport source_report{};
	std::string error{};
	REQUIRE(AshEngine::load_terrain_container(
		source_path, source, &source_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(source != nullptr);
	REQUIRE(source_report.loaded_generation == 1u);
	const bool source_materials_match = MaterialRegionsMatch(*source, error);
	REQUIRE_MESSAGE(source_materials_match, error);

	AshEngine::TerrainEditLayer weight_layer{};
	const bool weight_layer_built =
		BuildCanonicalWeightLayer(*source, weight_layer, error);
	REQUIRE_MESSAGE(weight_layer_built, error);
	REQUIRE(weight_layer.height_blocks.empty());
	AshEngine::TerrainWorkingSet working_set{};
	REQUIRE(AshEngine::make_terrain_working_set(*source, working_set, &error));
	REQUIRE_MESSAGE(error.empty(), error);
	source.reset();
	const bool ring_heights_applied = ApplyCanonicalRingHeights(working_set, error);
	REQUIRE_MESSAGE(ring_heights_applied, error);
	working_set.edit_layers.clear();
	working_set.edit_layers.push_back(std::move(weight_layer));

	working_set.dirty_components = MakeAllComponentCoords(working_set.layout);
	std::vector<AshEngine::TerrainDirtyComponentPayload> payloads{};
	payloads.reserve(working_set.dirty_components.size());
	constexpr size_t generator_component_batch_size = 128u;
	for (size_t batch_begin = 0u;
		batch_begin < working_set.dirty_components.size();
		batch_begin += generator_component_batch_size)
	{
		const size_t batch_end = std::min(
			batch_begin + generator_component_batch_size,
			working_set.dirty_components.size());
		const std::vector<AshEngine::TerrainComponentCoord> batch(
			working_set.dirty_components.begin() +
				static_cast<std::ptrdiff_t>(batch_begin),
			working_set.dirty_components.begin() +
				static_cast<std::ptrdiff_t>(batch_end));
		std::vector<AshEngine::TerrainDirtyComponentPayload> batch_payloads{};
		REQUIRE(AshEngine::compose_terrain_components(
			working_set, batch, batch_payloads, &error));
		REQUIRE_MESSAGE(error.empty(), error);
		REQUIRE(batch_payloads.size() == batch.size());
		for (auto& payload : batch_payloads)
		{
			REQUIRE(payload.component != nullptr);
			auto component =
				std::make_shared<AshEngine::TerrainComponentSnapshot>(*payload.component);
			component->lod_errors = k_terrain_gate_lod_errors;
			payload.component = std::move(component);
			const size_t component_index = static_cast<size_t>(payload.coord.z) *
				working_set.layout.component_count_x + payload.coord.x;
			REQUIRE(component_index < working_set.components.size());
			working_set.components[component_index] = payload.component;
			payloads.push_back(std::move(payload));
		}
	}
	REQUIRE(payloads.size() == working_set.dirty_components.size());

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> canonical{};
	REQUIRE(AshEngine::publish_terrain_working_set(
		working_set, payloads, canonical, &error));
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(canonical != nullptr);
	REQUIRE(canonical->edit_layers != nullptr);
	REQUIRE(canonical->edit_layers->size() == 1u);
	const bool canonical_layer_id_matches =
		canonical->edit_layers->front().id.bytes == k_terrain_gate_weight_layer_id;
	REQUIRE(canonical_layer_id_matches);
	REQUIRE(canonical->edit_layers->front().height_blocks.empty());
	const size_t expected_weight_block_count =
		k_terrain_gate_last_component_x - k_terrain_gate_first_component_x + 1u;
	REQUIRE(canonical->edit_layers->front().weight_blocks.size() ==
		expected_weight_block_count);
	const bool canonical_height_layout =
		TerrainGateHeightLayoutMatchesIndependentOracle(*canonical, error);
	REQUIRE_MESSAGE(canonical_height_layout, error);
	const uint64_t canonical_generation = canonical->content_generation;
	payloads.clear();
	payloads.shrink_to_fit();
	working_set = {};

	filesystem_error.clear();
	std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
	REQUIRE_MESSAGE(
		!filesystem_error,
		"Could not create Intermediate/generated-fixtures for TerrainGate staging.");
	const std::filesystem::path staging_directory =
		output_path.parent_path() / ".TerrainGate.generator-staging";
	const bool staging_directory_created =
		std::filesystem::create_directory(staging_directory, filesystem_error);
	const bool staging_directory_ready =
		!filesystem_error && staging_directory_created;
	REQUIRE_MESSAGE(
		staging_directory_ready,
		"Refusing to reuse an existing TerrainGate generator staging directory.");
	const std::filesystem::path staged_path =
		staging_directory / "TerrainGate.AshTerrain";
	ScopedStagingCleanup staging_cleanup{ staged_path, staging_directory };

	AshEngine::TerrainContainerSaveReport save_report{};
	REQUIRE(AshEngine::save_terrain_container_incremental(
		staged_path, *canonical, {}, &save_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(save_report.previous_generation == 0u);
	REQUIRE(save_report.committed_generation == canonical_generation);
	canonical.reset();

	std::shared_ptr<const AshEngine::TerrainAssetSnapshot> round_trip{};
	AshEngine::TerrainContainerLoadReport round_trip_report{};
	REQUIRE(AshEngine::load_terrain_container(
		staged_path, round_trip, &round_trip_report, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	REQUIRE(round_trip != nullptr);
	REQUIRE(round_trip_report.loaded_generation == canonical_generation);
	REQUIRE(round_trip->edit_layers != nullptr);
	REQUIRE(round_trip->edit_layers->size() == 1u);
	const bool round_trip_layer_id_matches =
		round_trip->edit_layers->front().id.bytes == k_terrain_gate_weight_layer_id;
	REQUIRE(round_trip_layer_id_matches);
	REQUIRE(round_trip->edit_layers->front().height_blocks.empty());
	const bool round_trip_height_layout =
		TerrainGateHeightLayoutMatchesIndependentOracle(*round_trip, error);
	REQUIRE_MESSAGE(round_trip_height_layout, error);
	const bool round_trip_canonical =
		RecompositionMatchesCanonicalSource(*round_trip, error);
	REQUIRE_MESSAGE(round_trip_canonical, error);
	const bool round_trip_lod = LodAutomationMetadataMatches(*round_trip, error);
	REQUIRE_MESSAGE(round_trip_lod, error);
	const bool round_trip_materials = MaterialRegionsMatch(*round_trip, error);
	REQUIRE_MESSAGE(round_trip_materials, error);

	REQUIRE(AshEngine::publish_staged_terrain_container_new(
		output_path, staged_path, &error) ==
		AshEngine::TerrainContainerResult::Success);
	REQUIRE_MESSAGE(error.empty(), error);
	filesystem_error.clear();
	CHECK_FALSE(std::filesystem::exists(staged_path, filesystem_error));
	REQUIRE_MESSAGE(
		!filesystem_error,
		"Could not inspect the TerrainGate staged source after publication.");
	filesystem_error.clear();
	const bool staging_directory_removed =
		std::filesystem::remove(staging_directory, filesystem_error);
	const bool staging_directory_cleaned =
		!filesystem_error && staging_directory_removed;
	REQUIRE_MESSAGE(
		staging_directory_cleaned,
		"Could not remove the empty TerrainGate generator staging directory.");
	filesystem_error.clear();
	CHECK(std::filesystem::exists(output_path, filesystem_error));
	CHECK_FALSE(filesystem_error);
}
