#pragma once

#include "Function/Asset/VegetationCodec.h"
#include "Function/Asset/VegetationChunk.h"
#include "Function/Asset/VegetationLayer.h"
#include "Function/Asset/VegetationSpecies.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Scene/VegetationSurfaceProvider.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <filesystem>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace VegetationTest
{
	class ScopedAssetRoot
	{
	public:
		explicit ScopedAssetRoot(const std::string& label)
		{
			static std::atomic<uint64_t> next_serial{ 1 };
			const uint64_t serial = next_serial.fetch_add(1, std::memory_order_relaxed);
			const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
			m_path = std::filesystem::current_path() / "Intermediate" / "test-temp" /
				"vegetation-task3" /
				(label + "-p" + std::to_string(_getpid()) + "-t" +
					std::to_string(timestamp) + "-s" + std::to_string(serial));
			std::error_code error{};
			if (!std::filesystem::create_directories(m_path, error) || error)
			{
				throw std::runtime_error("Failed to create scoped vegetation asset root");
			}
			m_owns_path = true;
		}

		~ScopedAssetRoot()
		{
			if (!m_owns_path)
			{
				return;
			}
			std::error_code error{};
			std::filesystem::remove_all(m_path, error);
		}

		ScopedAssetRoot(const ScopedAssetRoot&) = delete;
		ScopedAssetRoot& operator=(const ScopedAssetRoot&) = delete;

		const std::filesystem::path& Path() const
		{
			return m_path;
		}

		void Write(const std::filesystem::path& relative_path, const std::vector<uint8_t>& bytes)
		{
			const std::filesystem::path destination = m_path / relative_path;
			std::error_code error{};
			std::filesystem::create_directories(destination.parent_path(), error);
			if (error)
			{
				throw std::runtime_error("Failed to create scoped vegetation fixture directory");
			}
			std::ofstream output(destination, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				throw std::runtime_error("Failed to open scoped vegetation fixture");
			}
			if (!bytes.empty())
			{
				output.write(reinterpret_cast<const char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
			}
			if (!output)
			{
				throw std::runtime_error("Failed to write scoped vegetation fixture");
			}
		}

	private:
		std::filesystem::path m_path{};
		bool m_owns_path = false;
	};

	inline AshEngine::VegetationLoadBudget GenerousLoadBudget()
	{
		return { 64ull * 1024ull * 1024ull, 64ull * 1024ull * 1024ull,
			64ull * 1024ull * 1024ull, 65534u, 65534u, 1000000u };
	}

	inline std::vector<uint8_t> ReadFixtureBytes(const std::string& relative_path)
	{
		std::ifstream input(relative_path, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			throw std::runtime_error("Failed to open vegetation fixture: " + relative_path);
		}

		const std::streampos end_position = input.tellg();
		if (end_position < std::streampos(0))
		{
			throw std::runtime_error("Failed to size vegetation fixture: " + relative_path);
		}
		const std::streamoff byte_count = end_position - std::streampos(0);
		std::vector<uint8_t> bytes{};
		if (byte_count < 0 ||
			static_cast<uint64_t>(byte_count) > static_cast<uint64_t>(bytes.max_size()) ||
			byte_count > static_cast<std::streamoff>(
				std::numeric_limits<std::streamsize>::max()))
		{
			throw std::runtime_error("Vegetation fixture size is not representable: " + relative_path);
		}

		input.seekg(0, std::ios::beg);
		if (!input)
		{
			throw std::runtime_error("Failed to seek vegetation fixture: " + relative_path);
		}
		bytes.resize(static_cast<size_t>(byte_count));
		if (!bytes.empty())
		{
			const std::streamsize expected = static_cast<std::streamsize>(bytes.size());
			input.read(reinterpret_cast<char*>(bytes.data()), expected);
			if (input.gcount() != expected || input.bad() || input.fail())
			{
				throw std::runtime_error("Failed to read complete vegetation fixture: " + relative_path);
			}
		}

		char extra = 0;
		input.get(extra);
		if (input.gcount() == 1)
		{
			throw std::runtime_error("Vegetation fixture grew while reading: " + relative_path);
		}
		if (input.bad() || !input.eof())
		{
			throw std::runtime_error("Failed to confirm vegetation fixture EOF: " + relative_path);
		}
		return bytes;
	}

	inline void WriteAllBytes(
		const std::filesystem::path& path,
		const std::vector<uint8_t>& bytes)
	{
		std::error_code error{};
		const std::filesystem::path parent = path.parent_path();
		if (!parent.empty())
		{
			std::filesystem::create_directories(parent, error);
			if (error)
			{
				throw std::runtime_error("Failed to create vegetation test directory");
			}
		}

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.is_open())
		{
			throw std::runtime_error("Failed to open vegetation test file for writing");
		}
		if (!bytes.empty())
		{
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}
		if (!output)
		{
			throw std::runtime_error("Failed to write complete vegetation test file");
		}
	}

	inline std::vector<uint8_t> ReadAllBytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input.is_open())
		{
			throw std::runtime_error("Failed to open vegetation test file for reading");
		}

		const std::streampos end_position = input.tellg();
		if (end_position < std::streampos(0))
		{
			throw std::runtime_error("Failed to size vegetation test file");
		}
		const std::streamoff byte_count = end_position - std::streampos(0);
		if (byte_count < 0 ||
			static_cast<uint64_t>(byte_count) > static_cast<uint64_t>(
				std::vector<uint8_t>{}.max_size()) ||
			byte_count > static_cast<std::streamoff>(
				std::numeric_limits<std::streamsize>::max()))
		{
			throw std::runtime_error("Vegetation test file size is not representable");
		}

		std::vector<uint8_t> bytes(static_cast<size_t>(byte_count));
		input.seekg(0, std::ios::beg);
		if (!input)
		{
			throw std::runtime_error("Failed to seek vegetation test file");
		}
		if (!bytes.empty())
		{
			const std::streamsize expected = static_cast<std::streamsize>(bytes.size());
			input.read(reinterpret_cast<char*>(bytes.data()), expected);
			if (input.gcount() != expected || input.bad() || input.fail())
			{
				throw std::runtime_error("Failed to read complete vegetation test file");
			}
		}

		char extra = 0;
		input.get(extra);
		if (input.gcount() == 1 || input.bad() || !input.eof())
		{
			throw std::runtime_error("Vegetation test file changed while reading");
		}
		return bytes;
	}

	inline std::vector<uint8_t> CanonicalGrassSpeciesJson()
	{
		return ReadFixtureBytes(
			"project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation");
	}

	inline std::vector<uint8_t> ReplaceJsonToken(
		const std::vector<uint8_t>& bytes,
		const std::string& from,
		const std::string& to)
	{
		std::string text(bytes.begin(), bytes.end());
		const size_t position = text.find(from);
		if (position == std::string::npos ||
			text.find(from, position + from.size()) != std::string::npos)
		{
			throw std::runtime_error("JSON mutation token must occur exactly once");
		}
		text.replace(position, from.size(), to);
		return std::vector<uint8_t>(text.begin(), text.end());
	}

	inline AshEngine::VegetationId SequentialId(const uint8_t first = 1)
	{
		AshEngine::VegetationId id{};
		for (size_t index = 0; index < id.size(); ++index)
		{
			id[index] = static_cast<uint8_t>(first + index);
		}
		return id;
	}

	inline AshEngine::VegetationPaletteEntry MinimalPaletteEntry()
	{
		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id = SequentialId();
		const std::vector<uint8_t> canonical = CanonicalGrassSpeciesJson();
		entry.species_sha256 = AshEngine::vegetation_sha256(
			canonical.data(), canonical.size());
		entry.species_asset_path = "vegetation/Phase2ManualSpecies.AshVegetation";
		return entry;
	}

	inline AshEngine::VegetationLayerSnapshot MinimalLayerSnapshot()
	{
		AshEngine::VegetationLayerSnapshot layer{};
		layer.layer_id = SequentialId(33);
		layer.content_generation = 1;
		layer.layer_seed = 0x0123456789abcdefull;
		layer.palette.push_back(MinimalPaletteEntry());
		AshEngine::VegetationLayerTile tile{};
		tile.tile_x = -2;
		tile.tile_z = 3;
		AshEngine::VegetationLayerPlane density{};
		density.kind = AshEngine::VegetationLayerPlaneKind::Density;
		density.values.fill(0);
		density.values[0] = 255;
		tile.planes.push_back(density);
		AshEngine::VegetationLayerPlane weight{};
		weight.kind = AshEngine::VegetationLayerPlaneKind::SpeciesWeight;
		weight.species_id = layer.palette[0].species_id;
		weight.values.fill(0);
		weight.values[0] = 255;
		tile.planes.push_back(weight);
		layer.tiles.push_back(tile);
		return layer;
	}

	inline std::vector<uint8_t> MinimalLayerBytes()
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(MinimalLayerSnapshot(), bytes, &error))
		{
			throw std::runtime_error("Minimal vegetation Layer fixture did not encode: " + error);
		}
		return bytes;
	}

	inline std::vector<uint8_t> DifferentValidLayerBytes()
	{
		AshEngine::VegetationLayerSnapshot layer = MinimalLayerSnapshot();
		++layer.content_generation;
		layer.layer_seed ^= 0x55aa55aa55aa55aaull;
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(layer, bytes, &error))
		{
			throw std::runtime_error(
				"Different vegetation Layer fixture did not encode: " + error);
		}
		return bytes;
	}

	inline std::vector<uint8_t> CanonicalAuthoringPayloadBytes(
		const AshEngine::VegetationLayerSnapshot& snapshot)
	{
		AshEngine::VegetationLayerSnapshot canonical = snapshot;
		canonical.content_generation = 1;
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(canonical, bytes, &error) || bytes.size() < 80)
		{
			throw std::runtime_error(
				"Vegetation authoring payload fixture must be a canonical ASVL stream: " + error);
		}
		return std::vector<uint8_t>(bytes.begin() + 80, bytes.end());
	}

	inline AshEngine::VegetationChunk MinimalChunk()
	{
		AshEngine::VegetationChunk chunk{};
		chunk.layer_id = SequentialId(33);
		chunk.chunk_input_sha256.fill(0x5a);
		chunk.chunk = { -2, 3 };
		chunk.surface_identity.surface_id = SequentialId(65);
		chunk.surface_identity.content_revision = 4;
		chunk.surface_identity.residency_revision = 5;
		chunk.surface_identity.transform_revision = 6;
		chunk.species.push_back(MinimalPaletteEntry());
		AshEngine::VegetationChunkInstance instance{};
		instance.species_index = 0;
		instance.cell_x = 17;
		instance.cell_z = 29;
		instance.candidate_ordinal = 5;
		instance.cell_fraction_x_u16 = 65535;
		instance.cell_fraction_z_u16 = 32768;
		instance.yaw_turn_u16 = 0x8000;
		instance.scale_q12 = 4096;
		instance.normal_oct_x = 0;
		instance.normal_oct_y = 0;
		instance.world_height_mm = 1250;
		chunk.instances.push_back(instance);
		chunk.min_world_height_mm = 1250;
		chunk.max_world_height_mm = 1250;
		return chunk;
	}

	inline std::vector<uint8_t> MinimalChunkBytes()
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_chunk(MinimalChunk(), bytes, &error))
		{
			throw std::runtime_error("Minimal vegetation Chunk fixture did not encode: " + error);
		}
		return bytes;
	}

	inline AshEngine::VegetationPaletteEntry ResolvedMinimalPaletteEntry()
	{
		const std::vector<uint8_t> source = CanonicalGrassSpeciesJson();
		AshEngine::VegetationSpecies species{};
		std::string error{};
		if (!AshEngine::decode_vegetation_species(
			source, GenerousLoadBudget(), species, &error))
		{
			throw std::runtime_error(
				"Canonical vegetation Species fixture did not decode: " + error);
		}
		std::vector<uint8_t> canonical{};
		if (!AshEngine::encode_vegetation_species(species, canonical, &error))
		{
			throw std::runtime_error(
				"Canonical vegetation Species fixture did not re-encode: " + error);
		}

		AshEngine::VegetationPaletteEntry entry{};
		entry.species_id = species.species_id;
		entry.species_sha256 = AshEngine::vegetation_sha256(
			canonical.data(), canonical.size());
		entry.species_asset_path = "vegetation/Phase2ManualSpecies.AshVegetation";
		return entry;
	}

	inline AshEngine::VegetationLayerSnapshot ResolvedMinimalLayerSnapshot()
	{
		AshEngine::VegetationLayerSnapshot layer = MinimalLayerSnapshot();
		layer.palette[0] = ResolvedMinimalPaletteEntry();
		layer.tiles[0].planes[1].species_id = layer.palette[0].species_id;
		return layer;
	}

	inline std::vector<uint8_t> ResolvedMinimalLayerBytes()
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(ResolvedMinimalLayerSnapshot(), bytes, &error))
		{
			throw std::runtime_error(
				"Resolved vegetation Layer fixture did not encode: " + error);
		}
		return bytes;
	}

	inline AshEngine::VegetationChunk ResolvedMinimalChunk()
	{
		AshEngine::VegetationChunk chunk = MinimalChunk();
		chunk.species[0] = ResolvedMinimalPaletteEntry();
		return chunk;
	}

	inline std::vector<uint8_t> ResolvedMinimalChunkBytes()
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_chunk(ResolvedMinimalChunk(), bytes, &error))
		{
			throw std::runtime_error(
				"Resolved vegetation Chunk fixture did not encode: " + error);
		}
		return bytes;
	}

	inline void WriteU32LE(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value)
	{
		for (size_t index = 0; index < 4; ++index)
		{
			bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
		}
	}

	inline uint16_t ReadU16LE(const std::vector<uint8_t>& bytes, const size_t offset)
	{
		return static_cast<uint16_t>(bytes[offset]) |
			static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
	}

	inline uint32_t ReadU32LE(const std::vector<uint8_t>& bytes, const size_t offset)
	{
		uint32_t value = 0;
		for (size_t index = 0; index < 4; ++index)
		{
			value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
		}
		return value;
	}

	inline uint64_t ReadU64LE(const std::vector<uint8_t>& bytes, const size_t offset)
	{
		uint64_t value = 0;
		for (size_t index = 0; index < 8; ++index)
		{
			value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
		}
		return value;
	}

	inline void WriteU16LE(std::vector<uint8_t>& bytes, const size_t offset, const uint16_t value)
	{
		bytes[offset] = static_cast<uint8_t>(value);
		bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
	}

	inline void WriteU64LE(std::vector<uint8_t>& bytes, const size_t offset, const uint64_t value)
	{
		for (size_t index = 0; index < 8; ++index)
		{
			bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
		}
	}

	inline void RepairHeaderCrc(std::vector<uint8_t>& bytes, const size_t header_size,
		const size_t crc_offset)
	{
		WriteU32LE(bytes, crc_offset, 0);
		WriteU32LE(bytes, crc_offset, AshEngine::vegetation_crc32(bytes.data(), header_size));
	}

	inline void RepairChunkHeaderCrc(std::vector<uint8_t>& bytes)
	{
		RepairHeaderCrc(bytes, 160, 148);
	}

	inline void RepairLayerCrcs(std::vector<uint8_t>& bytes)
	{
		WriteU32LE(bytes, 68, AshEngine::vegetation_crc32(
			bytes.data() + 80, bytes.size() - 80));
		RepairHeaderCrc(bytes, 80, 72);
	}

	inline void RepairChunkCrcs(std::vector<uint8_t>& bytes)
	{
		WriteU32LE(bytes, 144, AshEngine::vegetation_crc32(
			bytes.data() + 160, bytes.size() - 160));
		RepairChunkHeaderCrc(bytes);
	}

	inline std::string ToHex(const AshEngine::VegetationSha256& digest)
	{
		std::ostringstream stream;
		stream << std::hex << std::setfill('0');
		for (const uint8_t byte : digest)
		{
			stream << std::setw(2) << static_cast<uint32_t>(byte);
		}
		return stream.str();
	}

	inline AshEngine::VegetationSurfaceIdentity SurfaceIdentity(
		const uint8_t first_byte = 1,
		const uint64_t content_revision = 10,
		const uint64_t residency_revision = 20,
		const uint64_t transform_revision = 30)
	{
		AshEngine::VegetationSurfaceIdentity identity{};
		for (size_t index = 0; index < identity.surface_id.size(); ++index)
		{
			identity.surface_id[index] = static_cast<uint8_t>(first_byte + index);
		}
		identity.content_revision = content_revision;
		identity.residency_revision = residency_revision;
		identity.transform_revision = transform_revision;
		return identity;
	}

	inline AshEngine::VegetationSurfaceSample ReadySurfaceSample(
		const uint32_t request_index,
		const double world_height_meters,
		const glm::dvec3& world_normal,
		const std::array<uint8_t, 8>& material_slot_weights = { 255, 0, 0, 0, 0, 0, 0, 0 })
	{
		AshEngine::VegetationSurfaceSample sample{};
		sample.request_index = request_index;
		sample.status = AshEngine::VegetationSurfaceStatus::Ready;
		sample.world_height_meters = world_height_meters;
		sample.world_normal = world_normal;
		sample.material_slot_weights = material_slot_weights;
		return sample;
	}

	inline AshEngine::VegetationSurfaceSample NonReadySurfaceSample(
		const uint32_t request_index,
		const AshEngine::VegetationSurfaceStatus status)
	{
		AshEngine::VegetationSurfaceSample sample{};
		sample.request_index = request_index;
		sample.status = status;
		return sample;
	}

	inline AshEngine::VegetationSurfaceSampleRequest SurfaceRequest(
		const double world_x,
		const double world_z)
	{
		AshEngine::VegetationSurfaceSampleRequest request{};
		AshEngine::split_vegetation_world_xz(
			glm::dvec2(world_x, world_z), request.chunk, request.local_xz);
		return request;
	}

	inline AshEngine::VegetationSurfaceSampleRequest SurfaceRequest(
		const AshEngine::VegetationChunkCoord chunk,
		const glm::dvec2& local_xz)
	{
		AshEngine::VegetationSurfaceSampleRequest request{};
		request.chunk = chunk;
		request.local_xz = local_xz;
		return request;
	}

	inline AshEngine::VegetationOperationControl ActiveControl(
		const std::chrono::milliseconds remaining)
	{
		AshEngine::VegetationOperationControl control{};
		control.cancel_requested = std::make_shared<std::atomic_bool>(false);
		control.deadline = std::chrono::steady_clock::now() + remaining;
		return control;
	}

	class ScriptedSurfaceSnapshot final : public AshEngine::IVegetationSurfaceSnapshot
	{
	public:
		AshEngine::VegetationSurfaceIdentity identity_before = SurfaceIdentity();
		AshEngine::VegetationSurfaceIdentity identity_after = identity_before;
		AshEngine::VegetationSurfaceBounds surface_bounds{
			AshEngine::VegetationChunkCoord{ -1024, -1024 },
			AshEngine::VegetationChunkCoord{ 1024, 1024 }
		};
		AshEngine::VegetationSurfaceBatchResult result{};
		bool throw_on_bounds = false;
		bool throw_on_sample = false;
		size_t throw_on_identity_call = 0;
		std::function<void(const AshEngine::VegetationOperationControl&)> before_sample_return{};

		mutable size_t bounds_call_count = 0;
		mutable size_t identity_call_count = 0;
		mutable size_t sample_call_count = 0;
		mutable size_t last_request_count = 0;
		mutable std::shared_ptr<const std::atomic_bool> last_cancel_requested{};
		mutable std::chrono::steady_clock::time_point last_deadline{};

		AshEngine::VegetationSurfaceIdentity identity() const override
		{
			++identity_call_count;
			if (throw_on_identity_call == identity_call_count)
			{
				throw std::runtime_error("scripted identity failure");
			}
			return identity_call_count == 1 ? identity_before : identity_after;
		}

		AshEngine::VegetationSurfaceBounds bounds() const override
		{
			++bounds_call_count;
			if (throw_on_bounds)
			{
				throw std::runtime_error("scripted bounds failure");
			}
			return surface_bounds;
		}

		AshEngine::VegetationSurfaceBatchResult sample_batch(
			const std::vector<AshEngine::VegetationSurfaceSampleRequest>& requests,
			AshEngine::VegetationOperationControl control) const override
		{
			++sample_call_count;
			last_request_count = requests.size();
			last_cancel_requested = control.cancel_requested;
			last_deadline = control.deadline;
			if (throw_on_sample)
			{
				throw std::runtime_error("scripted sample failure");
			}
			if (before_sample_return)
			{
				before_sample_return(control);
			}
			return result;
		}
	};

	class ScriptedSurfaceProvider final : public AshEngine::IVegetationSurfaceProvider
	{
	public:
		AshEngine::VegetationSurfaceCaptureResult result{};
		bool throw_on_capture = false;
		mutable size_t capture_call_count = 0;
		mutable AshEngine::VegetationSurfaceBinding last_binding{};

		AshEngine::VegetationSurfaceCaptureResult capture(
			const AshEngine::VegetationSurfaceBinding binding) const override
		{
			++capture_call_count;
			last_binding = binding;
			if (throw_on_capture)
			{
				throw std::runtime_error("scripted capture failure");
			}
			return result;
		}
	};
}
