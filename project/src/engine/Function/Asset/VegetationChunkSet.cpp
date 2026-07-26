#include "Function/Asset/VegetationChunkSet.h"

#include "Function/Asset/AssetDatabase.h"
#include "Function/Asset/VegetationAssetCodecInternal.h"
#include "Function/Asset/VegetationBaker.h"
#include "Function/Asset/VegetationChunk.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace AshEngine
{
	class VegetationChunkSetAccess
	{
	public:
		static decltype(auto) Data(VegetationPreparedChunkSet& prepared) noexcept
		{
			return (prepared.m_data);
		}

		static decltype(auto) Data(const VegetationPreparedChunkSet& prepared) noexcept
		{
			return (prepared.m_data);
		}
	};

	VegetationPreparedChunkSet::VegetationPreparedChunkSet(
		VegetationPreparedChunkSet&& other) noexcept
		: m_data(std::move(other.m_data))
	{
		other.m_data = Data{};
	}

	namespace
	{
		using namespace VegetationAssetCodecInternal;

		constexpr size_t asvm_header_bytes = 96;
		constexpr size_t asvm_entry_bytes = 80;
		constexpr size_t asva_bytes = 48;
		constexpr uint64_t empty_snapshot_summary_bytes = 112;
		constexpr uint64_t snapshot_entry_summary_bytes = 88;
		constexpr uint64_t snapshot_species_id_summary_bytes = 16;

		bool manifest_coord_less(
			const VegetationChunkCoord& lhs,
			const VegetationChunkCoord& rhs)
		{
			return lhs.z != rhs.z ? lhs.z < rhs.z : lhs.x < rhs.x;
		}

		bool manifest_is_canonical(const VegetationChunkSetManifest& manifest)
		{
			if (all_zero(manifest.layer_id) || manifest.layer_generation == 0 ||
				all_zero(manifest.surface_identity.surface_id) ||
				manifest.entries.size() > std::numeric_limits<uint32_t>::max())
			{
				return false;
			}
			for (size_t index = 0; index < manifest.entries.size(); ++index)
			{
				const VegetationChunkSetManifestEntry& entry = manifest.entries[index];
				if (all_zero(entry.object_sha256) || all_zero(entry.input_sha256) ||
					(index != 0 && !manifest_coord_less(
						manifest.entries[index - 1].coord, entry.coord)))
				{
					return false;
				}
			}
			return true;
		}

		void write_magic(ByteWriter& writer, const std::array<uint8_t, 4>& magic)
		{
			writer.write_array(magic);
		}

		bool has_layer_extension(const std::filesystem::path& path)
		{
			std::wstring extension = path.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](const wchar_t value)
				{
					return value >= L'A' && value <= L'Z'
						? static_cast<wchar_t>(value - L'A' + L'a') : value;
				});
			return extension == L".ashvegetationlayer";
		}

		bool is_canonical_rootless_relative_path(
			const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name() ||
				path.has_root_directory() || path.lexically_normal() != path)
			{
				return false;
			}
			for (const std::filesystem::path& component : path)
			{
				if (component.empty() || component == L"." || component == L"..")
				{
					return false;
				}
			}
			return true;
		}

		bool is_strict_lexical_descendant(
			const std::filesystem::path& candidate,
			const std::filesystem::path& root)
		{
			if (candidate.empty() || root.empty() || !candidate.is_absolute() ||
				!root.is_absolute() || candidate.lexically_normal() != candidate ||
				root.lexically_normal() != root)
			{
				return false;
			}
			auto candidate_component = candidate.begin();
			for (auto root_component = root.begin();
				root_component != root.end();
				++root_component, ++candidate_component)
			{
				if (candidate_component == candidate.end() ||
					*candidate_component != *root_component)
				{
					return false;
				}
			}
			return candidate_component != candidate.end();
		}

		bool valid_inspection_shape(const VegetationFileInspection& inspection)
		{
			const bool identity_cleared = !inspection.file_identity.available &&
				inspection.file_identity.volume_serial_number == 0 &&
				inspection.file_identity.file_index == 0;
			switch (inspection.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return !inspection.canonical_relative_path.empty() &&
					!inspection.resolved_absolute_path.empty() &&
					!inspection.canonical_identity.empty() &&
					(!inspection.is_regular_file || inspection.exists) &&
					(inspection.exists ? inspection.file_identity.available : identity_cleared);
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return inspection.canonical_relative_path.empty() &&
					inspection.resolved_absolute_path.empty() &&
					inspection.canonical_identity.empty() && identity_cleared &&
					!inspection.exists && !inspection.is_regular_file;
			default:
				return false;
			}
		}

		bool valid_inspection_binding(
			const VegetationFileInspection& inspection,
			const std::filesystem::path& asset_root,
			const std::filesystem::path& requested_relative_path)
		{
			if (!valid_inspection_shape(inspection))
			{
				return false;
			}
			if (inspection.status != VegetationFileResultStatus::Succeeded)
			{
				return true;
			}
			if (!is_canonical_rootless_relative_path(requested_relative_path) ||
				asset_root.empty() || !asset_root.is_absolute() ||
				asset_root.lexically_normal() != asset_root)
			{
				return false;
			}
			const std::filesystem::path expected_absolute =
				(asset_root / requested_relative_path).lexically_normal();
			return inspection.canonical_relative_path == requested_relative_path &&
				is_canonical_rootless_relative_path(
					inspection.canonical_relative_path) &&
				inspection.resolved_absolute_path == expected_absolute &&
				is_strict_lexical_descendant(
					inspection.resolved_absolute_path, asset_root);
		}

		bool valid_bytes_shape(
			const VegetationFileBytesResult& bytes,
			const uint64_t ceiling)
		{
			switch (bytes.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return bytes.bytes.size() <= ceiling;
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return bytes.bytes.empty();
			default:
				return false;
			}
		}

		bool control_active(const VegetationOperationControl& control)
		{
			return control.cancel_requested != nullptr &&
				control.deadline != std::chrono::steady_clock::time_point{} &&
				!control.cancel_requested->load(std::memory_order_acquire) &&
				std::chrono::steady_clock::now() < control.deadline;
		}

		bool check_control(
			const VegetationOperationControl& control,
			VegetationActiveChunkSetReadResult& result,
			const char* boundary)
		{
			if (control_active(control))
			{
				return true;
			}
			if (control.cancel_requested != nullptr &&
				control.cancel_requested->load(std::memory_order_acquire))
			{
				result.status = VegetationActiveChunkSetReadStatus::Cancelled;
			}
			else if (control.deadline != std::chrono::steady_clock::time_point{} &&
				std::chrono::steady_clock::now() >= control.deadline)
			{
				result.status = VegetationActiveChunkSetReadStatus::TimedOut;
			}
			else
			{
				result.status = VegetationActiveChunkSetReadStatus::Failed;
			}
			result.error = std::string("Vegetation chunk-set operation control failed at ") +
				boundary;
			return false;
		}

		void set_file_failure(
			VegetationActiveChunkSetReadResult& result,
			const VegetationFileResultStatus status,
			const std::string& error,
			const char* fallback)
		{
			result.status = status == VegetationFileResultStatus::Failed
				? VegetationActiveChunkSetReadStatus::Failed
				: VegetationActiveChunkSetReadStatus::Invalid;
			result.error = error.empty() ? fallback : error;
		}

		std::string lowercase_digest_hex(const VegetationSha256& digest)
		{
			constexpr char digits[] = "0123456789abcdef";
			std::string text{};
			text.reserve(64);
			for (const uint8_t byte : digest)
			{
				text.push_back(digits[byte >> 4]);
				text.push_back(digits[byte & 0x0fu]);
			}
			return text;
		}

		uint32_t read_u32_little_endian(
			const std::vector<uint8_t>& bytes,
			const size_t offset)
		{
			uint32_t value = 0;
			for (size_t index = 0; index < 4; ++index)
			{
				value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
			}
			return value;
		}

		constexpr uint64_t max_prepare_write_block_bytes = 1024ull * 1024ull;

		enum class PrepareControlState : uint8_t
		{
			Active = 0,
			Invalid,
			Cancelled,
			TimedOut
		};

		PrepareControlState prepare_control_state(
			const VegetationOperationControl& control)
		{
			if (!control.cancel_requested ||
				control.deadline == std::chrono::steady_clock::time_point{})
			{
				return PrepareControlState::Invalid;
			}
			if (control.cancel_requested->load(std::memory_order_acquire))
			{
				return PrepareControlState::Cancelled;
			}
			if (std::chrono::steady_clock::now() >= control.deadline)
			{
				return PrepareControlState::TimedOut;
			}
			return PrepareControlState::Active;
		}

		VegetationChunkSetPrepareStatus prepare_status(
			const PrepareControlState state)
		{
			switch (state)
			{
			case PrepareControlState::Cancelled:
				return VegetationChunkSetPrepareStatus::Cancelled;
			case PrepareControlState::TimedOut:
				return VegetationChunkSetPrepareStatus::TimedOut;
			case PrepareControlState::Invalid:
			default:
				return VegetationChunkSetPrepareStatus::Failed;
			}
		}

		VegetationChunkSetCommitStatus commit_status(
			const PrepareControlState state)
		{
			switch (state)
			{
			case PrepareControlState::Cancelled:
				return VegetationChunkSetCommitStatus::Cancelled;
			case PrepareControlState::TimedOut:
				return VegetationChunkSetCommitStatus::TimedOut;
			case PrepareControlState::Invalid:
			default:
				return VegetationChunkSetCommitStatus::Failed;
			}
		}

		bool check_prepare_control(
			const VegetationOperationControl& control,
			VegetationChunkSetPrepareStatus& out_status,
			std::string& out_error,
			const char* boundary)
		{
			const PrepareControlState state = prepare_control_state(control);
			if (state == PrepareControlState::Active)
			{
				return true;
			}
			out_status = prepare_status(state);
			out_error = std::string("Vegetation chunk-set preparation stopped at ") +
				boundary;
			return false;
		}

		bool valid_stage_shape(const VegetationStageFileResult& stage)
		{
			const bool identity_cleared = !stage.file_identity.available &&
				stage.file_identity.volume_serial_number == 0 &&
				stage.file_identity.file_index == 0;
			switch (stage.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return !stage.owned_stage_file.empty() &&
					stage.file_identity.available && stage.writer != nullptr;
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return stage.owned_stage_file.empty() && identity_cleared &&
					stage.writer == nullptr;
			default:
				return false;
			}
		}

		bool valid_stage_tree_shape(const VegetationStageTreeResult& stage)
		{
			const bool identity_cleared = !stage.file_identity.available &&
				stage.file_identity.volume_serial_number == 0 &&
				stage.file_identity.file_index == 0;
			switch (stage.status)
			{
			case VegetationFileResultStatus::Succeeded:
				return !stage.owned_stage_root.empty() &&
					stage.file_identity.available;
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return stage.owned_stage_root.empty() && identity_cleared;
			default:
				return false;
			}
		}

		bool valid_lease_shape(const VegetationFileLeaseResult& lease)
		{
			switch (lease.status)
			{
			case VegetationFileLeaseStatus::Acquired:
				return lease.lease != nullptr;
			case VegetationFileLeaseStatus::Cancelled:
			case VegetationFileLeaseStatus::TimedOut:
			case VegetationFileLeaseStatus::Failed:
				return lease.lease == nullptr;
			default:
				return false;
			}
		}

		bool valid_atomic_replace_shape(const VegetationAtomicReplaceResult& replace)
		{
			switch (replace.status)
			{
			case VegetationAtomicReplaceStatus::Replaced:
			case VegetationAtomicReplaceStatus::TargetPreserved:
				return replace.recovery_path.empty();
			case VegetationAtomicReplaceStatus::RecoveryRequired:
				return !replace.recovery_path.empty() &&
					replace.recovery_path.is_absolute() &&
					replace.recovery_path.lexically_normal() == replace.recovery_path;
			default:
				return false;
			}
		}

		bool legal_file_result_status(const VegetationFileResultStatus status)
		{
			switch (status)
			{
			case VegetationFileResultStatus::Succeeded:
			case VegetationFileResultStatus::NotFound:
			case VegetationFileResultStatus::InvalidPath:
			case VegetationFileResultStatus::LimitExceeded:
			case VegetationFileResultStatus::Failed:
				return true;
			default:
				return false;
			}
		}

		bool same_file_identity(
			const VegetationFileIdentity& lhs,
			const VegetationFileIdentity& rhs)
		{
			return lhs.available && rhs.available &&
				lhs.volume_serial_number == rhs.volume_serial_number &&
				lhs.file_index == rhs.file_index;
		}

		bool same_surface_identity(
			const VegetationSurfaceIdentity& lhs,
			const VegetationSurfaceIdentity& rhs)
		{
			return lhs.surface_id == rhs.surface_id &&
				lhs.content_revision == rhs.content_revision &&
				lhs.residency_revision == rhs.residency_revision &&
				lhs.transform_revision == rhs.transform_revision;
		}

		bool same_coord(
			const VegetationChunkCoord& lhs,
			const VegetationChunkCoord& rhs)
		{
			return lhs.x == rhs.x && lhs.z == rhs.z;
		}

		template<typename T, typename CoordOf>
		bool sorted_unique_coords(const std::vector<T>& values, CoordOf&& coord_of)
		{
			for (size_t index = 1; index < values.size(); ++index)
			{
				if (!manifest_coord_less(
					coord_of(values[index - 1]), coord_of(values[index])))
				{
					return false;
				}
			}
			return true;
		}

		bool valid_expected_identity_shape(
			const VegetationChunkSetExpectedIdentity& expected)
		{
			if (expected.operation_serial == 0 || expected.cooker_version != 1 ||
				expected.format_version != 1 || all_zero(expected.layer_id) ||
				expected.layer_generation == 0 ||
				all_zero(expected.surface_identity.surface_id) ||
				!sorted_unique_coords(expected.target_coords,
					[](const VegetationChunkCoord& coord) { return coord; }))
			{
				return false;
			}
			for (size_t index = 0; index < expected.species_identities.size(); ++index)
			{
				const VegetationChunkSetSpeciesIdentity& species =
					expected.species_identities[index];
				if (all_zero(species.species_id) || all_zero(species.canonical_sha256) ||
					(index != 0 && !(expected.species_identities[index - 1].species_id <
						species.species_id)))
				{
					return false;
				}
			}
			return true;
		}

		VegetationLoadBudget exact_chunk_validation_budget(const size_t byte_count)
		{
			VegetationLoadBudget budget{};
			budget.max_file_bytes = static_cast<uint64_t>(byte_count);
			budget.max_payload_bytes = static_cast<uint64_t>(byte_count);
			budget.max_decoded_bytes = std::numeric_limits<uint64_t>::max();
			budget.max_palette_records = std::numeric_limits<uint32_t>::max();
			budget.max_tile_records = std::numeric_limits<uint32_t>::max();
			budget.max_instance_records = std::numeric_limits<uint32_t>::max();
			return budget;
		}

		bool strict_chunk_matches(
			const std::vector<uint8_t>& bytes,
			const VegetationBakedChunk& baked,
			const VegetationChunkSetExpectedIdentity& expected,
			std::string& out_error)
		{
			if (bytes.empty() ||
				vegetation_sha256(bytes.data(), bytes.size()) != baked.object_sha256)
			{
				out_error = "Vegetation ASVC bytes do not match their object digest";
				return false;
			}
			VegetationChunk decoded{};
			std::string codec_error{};
			if (!decode_vegetation_chunk(
				bytes, exact_chunk_validation_budget(bytes.size()), decoded, &codec_error))
			{
				out_error = codec_error.empty()
					? "Vegetation ASVC strict decode failed" : std::move(codec_error);
				return false;
			}
			std::vector<uint8_t> canonical{};
			if (!encode_vegetation_chunk(decoded, canonical, &codec_error) ||
				canonical != bytes || !same_coord(decoded.chunk, baked.coord) ||
				decoded.cooker_version != expected.cooker_version ||
				decoded.layer_id != expected.layer_id ||
				!same_surface_identity(decoded.surface_identity, expected.surface_identity) ||
				decoded.chunk_input_sha256 != baked.input_digest)
			{
				out_error = "Vegetation ASVC canonical or cross-field identity mismatch";
				return false;
			}
			return true;
		}

		bool validate_prepare_transaction(
			const VegetationBakeResult& bake,
			std::vector<uint8_t>& out_manifest_bytes,
			VegetationSha256& out_manifest_sha256,
			std::string& out_error)
		{
			out_manifest_bytes.clear();
			out_manifest_sha256 = {};
			if (bake.status != VegetationBakeStatus::Succeeded ||
				!bake.transaction.has_value() || !bake.error.empty())
			{
				out_error = "Vegetation bake result has an illegal success capability shape";
				return false;
			}
			const VegetationBakeTransactionOutput& transaction = *bake.transaction;
			const VegetationChunkSetExpectedIdentity& expected =
				transaction.expected_identity;
			const bool no_active_source = transaction.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::NoActive;
			const bool existing_source = transaction.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::Existing;
			if ((!no_active_source && !existing_source) ||
				(no_active_source &&
					(!all_zero(transaction.source_active_identity.manifest_sha256) ||
						!transaction.full_rebake_required)) ||
				(existing_source &&
					all_zero(transaction.source_active_identity.manifest_sha256)) ||
				expected.operation_serial == 0 ||
				expected.cooker_version != 1 || expected.format_version != 1 ||
				all_zero(expected.layer_id) || expected.layer_generation == 0 ||
				all_zero(expected.surface_identity.surface_id))
			{
				out_error = "Vegetation transaction source identity is invalid";
				return false;
			}

			for (size_t index = 0; index < expected.species_identities.size(); ++index)
			{
				const VegetationChunkSetSpeciesIdentity& species =
					expected.species_identities[index];
				if (all_zero(species.species_id) || all_zero(species.canonical_sha256) ||
					(index != 0 && !(expected.species_identities[index - 1].species_id <
						species.species_id)))
				{
					out_error = "Vegetation transaction Species identities are not canonical";
					return false;
				}
			}
			uint64_t partition_size = 0;
			if (!checked_add(transaction.chunks.size(),
					transaction.removed_coords.size(), partition_size) ||
				partition_size > std::numeric_limits<uint32_t>::max() ||
				expected.target_coords.size() > std::numeric_limits<uint32_t>::max() ||
				!sorted_unique_coords(expected.target_coords,
					[](const VegetationChunkCoord& coord) { return coord; }) ||
				!sorted_unique_coords(transaction.chunks,
					[](const VegetationBakedChunk& chunk) { return chunk.coord; }) ||
				!sorted_unique_coords(transaction.removed_coords,
					[](const VegetationChunkCoord& coord) { return coord; }) ||
				!manifest_is_canonical(transaction.resulting_manifest) ||
				transaction.resulting_manifest.layer_id != expected.layer_id ||
				transaction.resulting_manifest.layer_generation !=
					expected.layer_generation ||
				!same_surface_identity(
					transaction.resulting_manifest.surface_identity,
					expected.surface_identity) ||
				expected.target_coords.size() != partition_size ||
				(no_active_source &&
					transaction.resulting_manifest.entries.size() !=
						transaction.chunks.size()))
			{
				out_error = "Vegetation transaction manifest or coordinate partition is invalid";
				return false;
			}

			std::vector<VegetationChunkCoord> partition{};
			partition.reserve(expected.target_coords.size());
			for (const VegetationBakedChunk& chunk : transaction.chunks)
			{
				partition.push_back(chunk.coord);
			}
			partition.insert(partition.end(), transaction.removed_coords.begin(),
				transaction.removed_coords.end());
			std::sort(partition.begin(), partition.end(), manifest_coord_less);
			if (partition.size() != expected.target_coords.size())
			{
				out_error = "Vegetation transaction coordinate partition size is invalid";
				return false;
			}
			for (size_t index = 0; index < partition.size(); ++index)
			{
				if (!same_coord(partition[index], expected.target_coords[index]) ||
					(index != 0 && same_coord(partition[index - 1], partition[index])))
				{
					out_error = "Vegetation transaction targets are not an exact partition";
					return false;
				}
			}

			for (size_t index = 0; index < transaction.chunks.size(); ++index)
			{
				const VegetationBakedChunk& baked = transaction.chunks[index];
				const auto entry = std::lower_bound(
					transaction.resulting_manifest.entries.begin(),
					transaction.resulting_manifest.entries.end(), baked.coord,
					[](const VegetationChunkSetManifestEntry& candidate,
						const VegetationChunkCoord& coord)
					{
						return manifest_coord_less(candidate.coord, coord);
					});
				if (!same_coord(baked.coord, baked.chunk.chunk) ||
					baked.input_digest != baked.chunk.chunk_input_sha256 ||
					all_zero(baked.input_digest) || all_zero(baked.object_sha256) ||
					entry == transaction.resulting_manifest.entries.end() ||
					!same_coord(entry->coord, baked.coord) ||
					entry->object_sha256 != baked.object_sha256 ||
					entry->input_sha256 != baked.input_digest ||
					baked.chunk.cooker_version != expected.cooker_version ||
					baked.chunk.layer_id != expected.layer_id ||
					!same_surface_identity(
						baked.chunk.surface_identity, expected.surface_identity))
				{
					out_error = "Vegetation baked chunk does not match the transaction manifest";
					return false;
				}
				std::vector<uint8_t> encoded_chunk{};
				std::string codec_error{};
				if (!encode_vegetation_chunk(baked.chunk, encoded_chunk, &codec_error) ||
					encoded_chunk != baked.object_bytes ||
					!strict_chunk_matches(
						baked.object_bytes, baked, expected, codec_error))
				{
					out_error = codec_error.empty()
						? "Vegetation baked ASVC payload is not canonical"
						: std::move(codec_error);
					return false;
				}
				for (const VegetationPaletteEntry& palette : baked.chunk.species)
				{
					const auto found = std::lower_bound(
						expected.species_identities.begin(),
						expected.species_identities.end(), palette.species_id,
						[](const VegetationChunkSetSpeciesIdentity& identity,
							const VegetationId& id)
						{
							return identity.species_id < id;
						});
					if (found == expected.species_identities.end() ||
						found->species_id != palette.species_id ||
						found->canonical_sha256 != palette.species_sha256)
					{
						out_error = "Vegetation ASVC Species identity is outside the bake capability";
						return false;
					}
				}
			}
			for (const VegetationChunkCoord& removed : transaction.removed_coords)
			{
				const auto entry = std::lower_bound(
					transaction.resulting_manifest.entries.begin(),
					transaction.resulting_manifest.entries.end(), removed,
					[](const VegetationChunkSetManifestEntry& candidate,
						const VegetationChunkCoord& coord)
					{
						return manifest_coord_less(candidate.coord, coord);
					});
				if (entry != transaction.resulting_manifest.entries.end() &&
					same_coord(entry->coord, removed))
				{
					out_error = "Vegetation removed coordinate remains in the resulting manifest";
					return false;
				}
			}

			std::string codec_error{};
			if (!encode_vegetation_chunk_set_manifest(
				transaction.resulting_manifest, out_manifest_bytes, &codec_error))
			{
				out_error = codec_error.empty()
					? "Vegetation ASVM encode failed" : std::move(codec_error);
				return false;
			}
			VegetationChunkSetManifest decoded_manifest{};
			if (!decode_vegetation_chunk_set_manifest(
				out_manifest_bytes,
				static_cast<uint32_t>(transaction.resulting_manifest.entries.size()),
				decoded_manifest, &codec_error))
			{
				out_error = codec_error.empty()
					? "Vegetation ASVM strict decode failed" : std::move(codec_error);
				out_manifest_bytes.clear();
				return false;
			}
			out_manifest_sha256 = vegetation_sha256(
				out_manifest_bytes.data(), out_manifest_bytes.size());
			return !all_zero(out_manifest_sha256);
		}

		bool validate_manifest_bytes(
			const std::vector<uint8_t>& bytes,
			const VegetationSha256& expected_sha256,
			const uint32_t expected_entry_count,
			std::string& out_error)
		{
			if (bytes.empty() ||
				vegetation_sha256(bytes.data(), bytes.size()) != expected_sha256)
			{
				out_error = "Vegetation ASVM bytes do not match their manifest digest";
				return false;
			}
			VegetationChunkSetManifest decoded{};
			if (!decode_vegetation_chunk_set_manifest(
				bytes, expected_entry_count, decoded, &out_error) ||
				decoded.entries.size() != expected_entry_count)
			{
				if (out_error.empty())
				{
					out_error = "Vegetation ASVM strict decode failed";
				}
				return false;
			}
			return true;
		}

		bool validate_existing_source_manifest(
			const std::filesystem::path& resolved_asset_root,
			const std::filesystem::path& store_relative_path,
			const VegetationBakeTransactionOutput& transaction,
			const VegetationOperationControl& control,
			IVegetationImmutablePublishFileOps& file_ops,
			VegetationChunkSetPrepareStatus& out_status,
			std::string& out_error)
		{
			const auto fail = [&](const char* error)
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = error;
				return false;
			};

			const uint64_t result_entry_count =
				static_cast<uint64_t>(transaction.resulting_manifest.entries.size());
			const uint64_t target_count =
				static_cast<uint64_t>(transaction.expected_identity.target_coords.size());
			uint64_t source_entry_ceiling = 0;
			uint64_t source_payload_ceiling = 0;
			uint64_t source_byte_ceiling = 0;
			if (!checked_add(result_entry_count, target_count, source_entry_ceiling) ||
				source_entry_ceiling > std::numeric_limits<uint32_t>::max() ||
				!checked_mul(source_entry_ceiling, asvm_entry_bytes,
					source_payload_ceiling) ||
				!checked_add(asvm_header_bytes, source_payload_ceiling,
					source_byte_ceiling))
			{
				return fail("Vegetation source ASVM verification ceiling is not representable");
			}

			const std::filesystem::path source_relative =
				(store_relative_path / L"manifests" /
					(lowercase_digest_hex(
						transaction.source_active_identity.manifest_sha256) + ".asvm"))
					.lexically_normal();
			if (!is_canonical_rootless_relative_path(source_relative) ||
				!check_prepare_control(
					control, out_status, out_error, "before source manifest inspection"))
			{
				if (out_error.empty())
				{
					out_status = VegetationChunkSetPrepareStatus::Failed;
					out_error = "Vegetation source ASVM path is not canonical";
				}
				return false;
			}

			const VegetationFileInspection source_inspection =
				file_ops.InspectPath(resolved_asset_root, source_relative);
			if (!check_prepare_control(
					control, out_status, out_error, "after source manifest inspection"))
			{
				return false;
			}
			const std::filesystem::path expected_source_absolute =
				(resolved_asset_root / source_relative).lexically_normal();
			if (!valid_inspection_shape(source_inspection) ||
				source_inspection.status != VegetationFileResultStatus::Succeeded ||
				!source_inspection.exists || !source_inspection.is_regular_file ||
				source_inspection.canonical_relative_path != source_relative ||
				source_inspection.resolved_absolute_path != expected_source_absolute ||
				!is_strict_lexical_descendant(
					source_inspection.resolved_absolute_path, resolved_asset_root))
			{
				return fail("Vegetation source ASVM inspection failed");
			}

			if (!check_prepare_control(
					control, out_status, out_error, "before source manifest read"))
			{
				return false;
			}
			const VegetationFileBytesResult source_bytes = file_ops.ReadAllBytes(
				source_inspection.resolved_absolute_path, source_byte_ceiling);
			if (!check_prepare_control(
					control, out_status, out_error, "after source manifest read"))
			{
				return false;
			}
			if (!valid_bytes_shape(source_bytes, source_byte_ceiling) ||
				source_bytes.status != VegetationFileResultStatus::Succeeded)
			{
				return fail("Vegetation source ASVM bounded read failed");
			}

			if (!check_prepare_control(
					control, out_status, out_error, "before source manifest hash"))
			{
				return false;
			}
			const VegetationSha256 source_sha256 = vegetation_sha256(
				source_bytes.bytes.data(), source_bytes.bytes.size());
			if (!check_prepare_control(
					control, out_status, out_error, "after source manifest hash"))
			{
				return false;
			}
			if (source_sha256 != transaction.source_active_identity.manifest_sha256)
			{
				return fail("Vegetation source ASVM bytes do not match their source digest");
			}

			if (!check_prepare_control(
					control, out_status, out_error, "before source manifest decode"))
			{
				return false;
			}
			VegetationChunkSetManifest source_manifest{};
			std::string codec_error{};
			const bool source_decoded = decode_vegetation_chunk_set_manifest(
					source_bytes.bytes, static_cast<uint32_t>(source_entry_ceiling),
					source_manifest, &codec_error);
			std::vector<uint8_t> canonical_source_bytes{};
			const bool source_canonical = source_decoded &&
				encode_vegetation_chunk_set_manifest(
					source_manifest, canonical_source_bytes, &codec_error) &&
				canonical_source_bytes == source_bytes.bytes;
			if (!check_prepare_control(
					control, out_status, out_error, "after source manifest decode"))
			{
				return false;
			}
			if (!source_decoded)
			{
				return fail("Vegetation source ASVM strict decode failed");
			}
			if (!source_canonical)
			{
				return fail("Vegetation source ASVM bytes are not canonical");
			}

			const std::vector<VegetationChunkCoord>& targets =
				transaction.expected_identity.target_coords;
			uint64_t merge_steps = 0;
			auto count_merge_step = [&]()
			{
				++merge_steps;
				return (merge_steps & 4095ull) != 0 || check_prepare_control(
					control, out_status, out_error, "during source manifest merge");
			};
			auto advance_to_untargeted = [&](const auto& entries,
				size_t& entry_index, size_t& target_index)
			{
				while (entry_index < entries.size())
				{
					const VegetationChunkCoord& coord = entries[entry_index].coord;
					while (target_index < targets.size() &&
						manifest_coord_less(targets[target_index], coord))
					{
						++target_index;
						if (!count_merge_step())
						{
							return -1;
						}
					}
					if (target_index < targets.size() &&
						same_coord(targets[target_index], coord))
					{
						++entry_index;
						++target_index;
						if (!count_merge_step())
						{
							return -1;
						}
						continue;
					}
					return 1;
				}
				return 0;
			};

			size_t source_index = 0;
			size_t result_index = 0;
			size_t source_target_index = 0;
			size_t result_target_index = 0;
			bool source_has_untouched_entry = false;
			if (!check_prepare_control(
					control, out_status, out_error, "before source manifest merge"))
			{
				return false;
			}
			while (true)
			{
				const int source_available = advance_to_untargeted(
					source_manifest.entries, source_index, source_target_index);
				if (source_available < 0)
				{
					return false;
				}
				const int result_available = advance_to_untargeted(
					transaction.resulting_manifest.entries,
					result_index, result_target_index);
				if (result_available < 0)
				{
					return false;
				}
				if (!source_available || !result_available)
				{
					if (source_available != result_available)
					{
						return fail("Vegetation resulting ASVM changed a non-target entry");
					}
					break;
				}
				const VegetationChunkSetManifestEntry& source_entry =
					source_manifest.entries[source_index];
				const VegetationChunkSetManifestEntry& result_entry =
					transaction.resulting_manifest.entries[result_index];
				if (!same_coord(source_entry.coord, result_entry.coord) ||
					source_entry.object_sha256 != result_entry.object_sha256 ||
					source_entry.input_sha256 != result_entry.input_sha256)
				{
					return fail("Vegetation resulting ASVM changed a non-target entry");
				}
				source_has_untouched_entry = true;
				++source_index;
				++result_index;
				if (!count_merge_step())
				{
					return false;
				}
			}

			const bool stable_content_identity =
				source_manifest.layer_id == transaction.expected_identity.layer_id &&
				same_surface_identity(source_manifest.surface_identity,
					transaction.expected_identity.surface_identity);
			if ((source_has_untouched_entry && !stable_content_identity) ||
				(!source_has_untouched_entry && !stable_content_identity &&
					!transaction.full_rebake_required))
			{
				return fail("Vegetation source ASVM identity change is incompatible with its untouched entries");
			}
			return check_prepare_control(
				control, out_status, out_error, "after source manifest merge");
		}

		template<typename StrictValidator>
		bool stage_and_publish_immutable(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& stage_tree,
			const std::filesystem::path& stage_child,
			const std::filesystem::path& target_relative,
			const std::filesystem::path& target_absolute,
			const std::vector<uint8_t>& expected_bytes,
			const VegetationSha256& expected_sha256,
			const VegetationOperationControl& control,
			IVegetationImmutablePublishFileOps& file_ops,
			StrictValidator&& strict_validator,
			VegetationChunkSetPrepareStatus& out_status,
			std::string& out_error)
		{
			if (!check_prepare_control(
				control, out_status, out_error, "before immutable stage creation"))
			{
				return false;
			}
			VegetationStageFileResult stage =
				file_ops.CreateOwnedStageFile(stage_tree, stage_child);
			if (!valid_stage_shape(stage))
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = "Vegetation immutable child stage returned an illegal shape";
				return false;
			}
			if (stage.status != VegetationFileResultStatus::Succeeded)
			{
				out_status = stage.status == VegetationFileResultStatus::InvalidPath
					? VegetationChunkSetPrepareStatus::InvalidPath
					: VegetationChunkSetPrepareStatus::Failed;
				out_error = stage.error.empty()
					? "Vegetation immutable child stage creation failed" : stage.error;
				return false;
			}
			if (!stage.owned_stage_file.is_absolute() ||
				stage.owned_stage_file.lexically_normal() != stage.owned_stage_file ||
				stage.owned_stage_file.parent_path() != stage_tree ||
				stage.owned_stage_file == target_absolute || expected_bytes.empty())
			{
				stage.writer.reset();
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = "Vegetation immutable child stage is not a distinct normalized tree child";
				return false;
			}

			uint64_t offset = 0;
			while (offset < expected_bytes.size())
			{
				if (!check_prepare_control(
					control, out_status, out_error, "before immutable stage write"))
				{
					stage.writer.reset();
					return false;
				}
				const uint64_t remaining =
					static_cast<uint64_t>(expected_bytes.size()) - offset;
				const size_t block_size = static_cast<size_t>(
					std::min<uint64_t>(remaining, max_prepare_write_block_bytes));
				if (block_size == 0 || block_size > max_prepare_write_block_bytes ||
					!stage.writer->WriteBlock(offset,
						{ expected_bytes.data() + static_cast<size_t>(offset), block_size }))
				{
					stage.writer.reset();
					out_status = VegetationChunkSetPrepareStatus::Failed;
					out_error = "Vegetation immutable stage write failed";
					return false;
				}
				offset += block_size;
				if (!check_prepare_control(
					control, out_status, out_error, "after immutable stage write"))
				{
					stage.writer.reset();
					return false;
				}
			}
			if (!stage.writer->FlushAndClose())
			{
				stage.writer.reset();
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = "Vegetation immutable stage flush failed";
				return false;
			}
			stage.writer.reset();

			const VegetationFileBytesResult stage_readback = file_ops.ReadAllBytes(
				stage.owned_stage_file, static_cast<uint64_t>(expected_bytes.size()));
			if (!valid_bytes_shape(
					stage_readback, static_cast<uint64_t>(expected_bytes.size())) ||
				stage_readback.status != VegetationFileResultStatus::Succeeded ||
				stage_readback.bytes != expected_bytes ||
				vegetation_sha256(
					stage_readback.bytes.data(), stage_readback.bytes.size()) !=
					expected_sha256 ||
				!strict_validator(stage_readback.bytes, out_error))
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				if (out_error.empty())
				{
					out_error = "Vegetation immutable stage readback failed strict validation";
				}
				return false;
			}

			if (!check_prepare_control(
				control, out_status, out_error, "before immutable publication"))
			{
				return false;
			}
			const VegetationCreateNewStatus publication =
				file_ops.PublishImmutableFromStage(
					stage.owned_stage_file, target_absolute);
			if (publication != VegetationCreateNewStatus::Created &&
				publication != VegetationCreateNewStatus::AlreadyExists)
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = "Vegetation immutable publication failed";
				return false;
			}

			const VegetationFileInspection target_inspection =
				file_ops.InspectPath(asset_root, target_relative);
			if (!valid_inspection_shape(target_inspection) ||
				target_inspection.status != VegetationFileResultStatus::Succeeded ||
				!target_inspection.exists || !target_inspection.is_regular_file ||
				target_inspection.canonical_relative_path != target_relative ||
				target_inspection.resolved_absolute_path != target_absolute ||
				(publication == VegetationCreateNewStatus::Created &&
					!same_file_identity(
						stage.file_identity, target_inspection.file_identity)) ||
				(publication == VegetationCreateNewStatus::AlreadyExists &&
					same_file_identity(
						stage.file_identity, target_inspection.file_identity)))
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				out_error = "Vegetation immutable target reinspection failed";
				return false;
			}
			const VegetationFileBytesResult target_readback = file_ops.ReadAllBytes(
				target_inspection.resolved_absolute_path,
				static_cast<uint64_t>(expected_bytes.size()));
			if (!valid_bytes_shape(
					target_readback, static_cast<uint64_t>(expected_bytes.size())) ||
				target_readback.status != VegetationFileResultStatus::Succeeded ||
				target_readback.bytes != expected_bytes ||
				vegetation_sha256(
					target_readback.bytes.data(), target_readback.bytes.size()) !=
					expected_sha256 ||
				!strict_validator(target_readback.bytes, out_error))
			{
				out_status = VegetationChunkSetPrepareStatus::Failed;
				if (out_error.empty())
				{
					out_error = "Vegetation immutable target bytes do not match the transaction";
				}
				return false;
			}
			return true;
		}
	}

	VegetationPreparedChunkSet prepare_vegetation_chunk_set(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationBakeResult& bake,
		const VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationImmutablePublishFileOps& file_ops)
	{
		VegetationPreparedChunkSet result{};
		auto& data = VegetationChunkSetAccess::Data(result);
		std::filesystem::path stage_tree{};
		std::unique_ptr<IVegetationStageFileWriter> active_writer{};

		auto set_failure = [&](const VegetationChunkSetPrepareStatus status,
			std::string error)
		{
			data.status = status;
			data.error = std::move(error);
		};
		auto fail_with_tree = [&](const VegetationChunkSetPrepareStatus status,
			std::string error)
		{
			active_writer.reset();
			if (!stage_tree.empty() && cleanup_registry.OwnsStageTree(stage_tree))
			{
				if (!cleanup_registry.CleanupStageTree(stage_tree, file_ops))
				{
					data.status = VegetationChunkSetPrepareStatus::RecoveryRequired;
					data.error = std::move(error) +
						"; immutable stage-tree cleanup failed";
					return;
				}
				stage_tree.clear();
			}
			set_failure(status, std::move(error));
		};
		auto fail_with_active_stage = [&](const VegetationChunkSetPrepareStatus status,
			std::string error)
		{
			active_writer.reset();
			if (!data.stage_path.empty())
			{
				if (cleanup_registry.IsRecoveryStageFile(data.stage_path))
				{
					data.status = VegetationChunkSetPrepareStatus::RecoveryRequired;
					data.error = std::move(error) +
						"; independently verified active pointer stage remains retained for recovery";
					return;
				}
				if (cleanup_registry.OwnsStageFile(data.stage_path))
				{
					if (!cleanup_registry.CleanupStageFile(data.stage_path, file_ops))
					{
						data.status = VegetationChunkSetPrepareStatus::RecoveryRequired;
						data.error = std::move(error) +
							"; active pointer stage cleanup failed";
						return;
					}
				}
			}
			data.stage_path.clear();
			data.stage_file_identity = {};
			data.active_stage_size = 0;
			data.active_stage_sha256 = {};
			data.cleanup_registry = nullptr;
			set_failure(status, std::move(error));
		};

		try
		{
			std::vector<uint8_t> manifest_bytes{};
			VegetationSha256 manifest_sha256{};
			std::string validation_error{};
			if (!validate_prepare_transaction(
				bake, manifest_bytes, manifest_sha256, validation_error))
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					validation_error.empty()
						? "Vegetation bake transaction is invalid"
						: std::move(validation_error));
				return result;
			}
			const VegetationBakeTransactionOutput& transaction = *bake.transaction;
			const uint64_t operation_serial =
				transaction.expected_identity.operation_serial;

			VegetationChunkSetActivePointer active_pointer{};
			active_pointer.manifest_sha256 = manifest_sha256;
			std::vector<uint8_t> active_bytes{};
			std::string codec_error{};
			if (!encode_vegetation_chunk_set_active_pointer(
				active_pointer, active_bytes, &codec_error) || active_bytes.size() != asva_bytes)
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					codec_error.empty()
						? "Vegetation active pointer encode failed"
						: std::move(codec_error));
				return result;
			}
			const VegetationSha256 active_sha256 = vegetation_sha256(
				active_bytes.data(), active_bytes.size());

			VegetationChunkSetPrepareStatus operation_status =
				VegetationChunkSetPrepareStatus::Failed;
			std::string operation_error{};
			if (!check_prepare_control(
				control, operation_status, operation_error, "before layer inspection"))
			{
				set_failure(operation_status, std::move(operation_error));
				return result;
			}
			if (!is_canonical_rootless_relative_path(layer_path) ||
				!has_layer_extension(layer_path))
			{
				set_failure(VegetationChunkSetPrepareStatus::InvalidPath,
					"Vegetation chunk-set path must be a canonical rootless AshVegetationLayer asset path");
				return result;
			}
			std::error_code absolute_error{};
			const std::filesystem::path resolved_asset_root =
				std::filesystem::absolute(asset_root, absolute_error).lexically_normal();
			if (absolute_error || resolved_asset_root.empty() ||
				!resolved_asset_root.is_absolute())
			{
				set_failure(VegetationChunkSetPrepareStatus::InvalidPath,
					"Vegetation asset root could not be resolved");
				return result;
			}
			const VegetationFileInspection layer_inspection =
				file_ops.InspectPath(resolved_asset_root, layer_path);
			if (!valid_inspection_shape(layer_inspection))
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation Layer inspection returned an illegal shape");
				return result;
			}
			if (layer_inspection.status != VegetationFileResultStatus::Succeeded ||
				!layer_inspection.exists || !layer_inspection.is_regular_file ||
				!has_layer_extension(layer_inspection.canonical_relative_path))
			{
				set_failure(
					layer_inspection.status == VegetationFileResultStatus::InvalidPath ||
						layer_inspection.status == VegetationFileResultStatus::NotFound ||
						!layer_inspection.exists || !layer_inspection.is_regular_file
						? VegetationChunkSetPrepareStatus::InvalidPath
						: VegetationChunkSetPrepareStatus::Failed,
					layer_inspection.error.empty()
						? "Vegetation Layer path is missing or invalid"
						: layer_inspection.error);
				return result;
			}

			if (!is_canonical_rootless_relative_path(
					layer_inspection.canonical_relative_path) ||
				layer_inspection.canonical_relative_path != layer_path ||
				!is_strict_lexical_descendant(
					layer_inspection.resolved_absolute_path, resolved_asset_root) ||
				layer_inspection.resolved_absolute_path !=
					(resolved_asset_root /
						layer_inspection.canonical_relative_path).lexically_normal())
			{
				set_failure(VegetationChunkSetPrepareStatus::InvalidPath,
					"Vegetation Layer inspection did not resolve to its canonical asset path");
				return result;
			}
			data.asset_root = resolved_asset_root;
			data.layer_canonical_relative_path =
				layer_inspection.canonical_relative_path;
			data.layer_resolved_absolute_path = layer_inspection.resolved_absolute_path;
			data.layer_canonical_identity = layer_inspection.canonical_identity;
			data.layer_file_identity = layer_inspection.file_identity;

			std::filesystem::path requested_store =
				layer_inspection.canonical_relative_path;
			requested_store += ".AshVegetationChunks";
			if (transaction.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::Existing)
			{
				operation_error.clear();
				if (!validate_existing_source_manifest(
						resolved_asset_root, requested_store, transaction,
						control, file_ops, operation_status, operation_error))
				{
					set_failure(operation_status,
						operation_error.empty()
							? "Vegetation existing source manifest verification failed"
							: std::move(operation_error));
					return result;
				}
			}
			const std::array<std::filesystem::path, 4> required_directories = {
				requested_store,
				requested_store / L"objects",
				requested_store / L"manifests",
				requested_store / L"staging"
			};
			for (const std::filesystem::path& directory : required_directories)
			{
				if (!check_prepare_control(
					control, operation_status, operation_error,
					"before immutable directory creation"))
				{
					set_failure(operation_status, std::move(operation_error));
					return result;
				}
				const VegetationFileResultStatus directory_status =
					file_ops.EnsureDirectoryTree(resolved_asset_root, directory);
				if (!legal_file_result_status(directory_status) ||
					directory_status != VegetationFileResultStatus::Succeeded)
				{
					set_failure(
						directory_status == VegetationFileResultStatus::InvalidPath
							? VegetationChunkSetPrepareStatus::InvalidPath
							: VegetationChunkSetPrepareStatus::Failed,
						"Vegetation chunk store directory creation failed");
					return result;
				}
			}

			const VegetationFileInspection store_inspection =
				file_ops.InspectPath(resolved_asset_root, requested_store);
			if (!valid_inspection_shape(store_inspection) ||
				store_inspection.status != VegetationFileResultStatus::Succeeded ||
				!store_inspection.exists || store_inspection.is_regular_file ||
				store_inspection.canonical_relative_path != requested_store ||
				store_inspection.resolved_absolute_path !=
					(data.asset_root / requested_store).lexically_normal())
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk store inspection failed");
				return result;
			}
			data.store_canonical_relative_path =
				store_inspection.canonical_relative_path;
			data.store_resolved_absolute_path = store_inspection.resolved_absolute_path;
			data.store_canonical_identity = store_inspection.canonical_identity;
			data.store_file_identity = store_inspection.file_identity;

			const std::filesystem::path staging_absolute =
				(data.store_resolved_absolute_path / L"staging").lexically_normal();
			VegetationStageTreeResult tree =
				file_ops.CreateUniqueStageTree(staging_absolute, operation_serial);
			if (!valid_stage_tree_shape(tree))
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation immutable stage tree returned an illegal shape");
				return result;
			}
			if (tree.status != VegetationFileResultStatus::Succeeded)
			{
				set_failure(
					tree.status == VegetationFileResultStatus::InvalidPath
						? VegetationChunkSetPrepareStatus::InvalidPath
						: VegetationChunkSetPrepareStatus::Failed,
					tree.error.empty()
						? "Vegetation immutable stage tree creation failed" : tree.error);
				return result;
			}
			stage_tree = tree.owned_stage_root;
			if (!stage_tree.is_absolute() ||
				stage_tree.lexically_normal() != stage_tree ||
				stage_tree.parent_path() != staging_absolute)
			{
				stage_tree.clear();
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation immutable stage tree is not a normalized staging child");
				return result;
			}
			if (!cleanup_registry.TrackStageTree(stage_tree, tree.file_identity))
			{
				stage_tree.clear();
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation immutable stage-tree ownership registration failed");
				return result;
			}

			for (size_t index = 0; index < transaction.chunks.size(); ++index)
			{
				const VegetationBakedChunk& baked_chunk = transaction.chunks[index];
				const std::string object_name =
					lowercase_digest_hex(baked_chunk.object_sha256) +
					".AshVegetationChunk";
				const std::filesystem::path object_relative =
					data.store_canonical_relative_path / L"objects" / object_name;
				const std::filesystem::path object_absolute =
					(data.store_resolved_absolute_path / L"objects" /
						object_name).lexically_normal();
				const std::filesystem::path stage_child =
					L".ashveg-layer-stage-object-" + std::to_wstring(index) + L".tmp";
				operation_error.clear();
				if (!stage_and_publish_immutable(
						resolved_asset_root, stage_tree, stage_child,
						object_relative, object_absolute,
						baked_chunk.object_bytes, baked_chunk.object_sha256,
						control, file_ops,
						[&](const std::vector<uint8_t>& bytes, std::string& error)
						{
							return strict_chunk_matches(bytes, baked_chunk,
								transaction.expected_identity, error);
						},
						operation_status, operation_error))
				{
					fail_with_tree(operation_status,
						operation_error.empty()
							? "Vegetation immutable object preparation failed"
							: std::move(operation_error));
					return result;
				}
			}

			const std::string manifest_name =
				lowercase_digest_hex(manifest_sha256) + ".asvm";
			const std::filesystem::path manifest_relative =
				data.store_canonical_relative_path / L"manifests" / manifest_name;
			const std::filesystem::path manifest_absolute =
				(data.store_resolved_absolute_path / L"manifests" /
					manifest_name).lexically_normal();
			operation_error.clear();
			if (!stage_and_publish_immutable(
					resolved_asset_root, stage_tree,
					L".ashveg-layer-stage-manifest.tmp",
					manifest_relative, manifest_absolute,
					manifest_bytes, manifest_sha256, control, file_ops,
					[&](const std::vector<uint8_t>& bytes, std::string& error)
					{
						return validate_manifest_bytes(bytes, manifest_sha256,
							static_cast<uint32_t>(
								transaction.resulting_manifest.entries.size()), error);
					},
					operation_status, operation_error))
			{
				fail_with_tree(operation_status,
					operation_error.empty()
						? "Vegetation immutable manifest preparation failed"
						: std::move(operation_error));
				return result;
			}

			if (!cleanup_registry.CleanupStageTree(stage_tree, file_ops))
			{
				set_failure(VegetationChunkSetPrepareStatus::RecoveryRequired,
					"Vegetation immutable stage-tree cleanup failed after publication");
				return result;
			}
			stage_tree.clear();

			const std::filesystem::path requested_active =
				data.store_canonical_relative_path / L"active.asva";
			const VegetationFileInspection initial_active =
				file_ops.InspectPath(resolved_asset_root, requested_active);
			const bool existing_source = transaction.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::Existing;
			const bool valid_initial_source_state = existing_source
				? initial_active.exists && initial_active.is_regular_file
				: !initial_active.exists && !initial_active.is_regular_file;
			if (!valid_inspection_shape(initial_active) ||
				initial_active.status != VegetationFileResultStatus::Succeeded ||
				!valid_initial_source_state ||
				initial_active.canonical_relative_path != requested_active ||
				initial_active.resolved_absolute_path !=
					(data.store_resolved_absolute_path / L"active.asva").lexically_normal())
			{
				set_failure(
					initial_active.status == VegetationFileResultStatus::InvalidPath
						? VegetationChunkSetPrepareStatus::InvalidPath
						: VegetationChunkSetPrepareStatus::Failed,
					existing_source
						? "Vegetation existing active target inspection failed"
						: "Vegetation no-active target inspection failed");
				return result;
			}
			data.active_canonical_relative_path =
				initial_active.canonical_relative_path;
			data.active_resolved_absolute_path = initial_active.resolved_absolute_path;
			data.active_canonical_identity = initial_active.canonical_identity;

			VegetationStageFileResult active_stage =
				file_ops.CreateUniqueSiblingStageFile(
					data.active_resolved_absolute_path, operation_serial);
			if (!valid_stage_shape(active_stage))
			{
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active stage returned an illegal shape");
				return result;
			}
			if (active_stage.status != VegetationFileResultStatus::Succeeded)
			{
				set_failure(
					active_stage.status == VegetationFileResultStatus::InvalidPath
						? VegetationChunkSetPrepareStatus::InvalidPath
						: VegetationChunkSetPrepareStatus::Failed,
					active_stage.error.empty()
						? "Vegetation active stage creation failed" : active_stage.error);
				return result;
			}
			std::filesystem::path created_active_stage =
				std::move(active_stage.owned_stage_file);
			const VegetationFileIdentity created_active_identity =
				active_stage.file_identity;
			active_writer = std::move(active_stage.writer);
			if (!created_active_stage.is_absolute() ||
				created_active_stage.lexically_normal() != created_active_stage ||
				created_active_stage == data.active_resolved_absolute_path ||
				created_active_stage.parent_path() !=
					data.active_resolved_absolute_path.parent_path() ||
				same_file_identity(initial_active.file_identity, created_active_identity))
			{
				active_writer.reset();
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active stage is not a distinct normalized target sibling");
				return result;
			}

			auto retain_verified_active_stage_for_recovery =
				[&](std::string error)
				{
					active_writer.reset();
					bool verified_new_stage = false;
					try
					{
						const std::filesystem::path stage_relative =
							(initial_active.canonical_relative_path.parent_path() /
								created_active_stage.filename()).lexically_normal();
						if (is_canonical_rootless_relative_path(stage_relative) &&
							is_strict_lexical_descendant(
								created_active_stage, data.asset_root) &&
							created_active_stage ==
								(data.asset_root / stage_relative).lexically_normal())
						{
							const VegetationFileInspection stage_inspection =
								file_ops.InspectPath(
									resolved_asset_root, stage_relative);
							verified_new_stage =
								valid_inspection_shape(stage_inspection) &&
								stage_inspection.status ==
									VegetationFileResultStatus::Succeeded &&
								stage_inspection.exists &&
								stage_inspection.is_regular_file &&
								stage_inspection.canonical_relative_path == stage_relative &&
								stage_inspection.resolved_absolute_path ==
									created_active_stage &&
								same_file_identity(
									stage_inspection.file_identity,
									created_active_identity);
						}
					}
					catch (...)
					{
						verified_new_stage = false;
					}

					bool tracked_for_recovery = false;
					if (verified_new_stage)
					{
						try
						{
							tracked_for_recovery =
								cleanup_registry.TrackNewRecoveryStageFile(
									created_active_stage, created_active_identity);
						}
						catch (...)
						{
							tracked_for_recovery = false;
						}
					}
					if (tracked_for_recovery)
					{
						data.stage_path = std::move(created_active_stage);
						data.stage_file_identity = created_active_identity;
						data.cleanup_registry = &cleanup_registry;
						set_failure(VegetationChunkSetPrepareStatus::RecoveryRequired,
							std::move(error) +
								"; the independently verified active stage is retained for recovery");
						return;
					}
					set_failure(VegetationChunkSetPrepareStatus::Failed,
						std::move(error) +
							"; no new exact active-stage ownership could be proven");
				};

			VegetationFileInspection post_stage_active{};
			bool post_stage_inspection_succeeded = false;
			try
			{
				post_stage_active = file_ops.InspectPath(
					resolved_asset_root, requested_active);
				post_stage_inspection_succeeded =
					valid_inspection_shape(post_stage_active) &&
					post_stage_active.status == VegetationFileResultStatus::Succeeded;
			}
			catch (...)
			{
				post_stage_inspection_succeeded = false;
			}
			if (!post_stage_inspection_succeeded)
			{
				retain_verified_active_stage_for_recovery(
					"Vegetation active target reinspection failed");
				return result;
			}
			if (post_stage_active.exists && same_file_identity(
				post_stage_active.file_identity, created_active_identity))
			{
				active_writer.reset();
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active stage aliases its target after creation");
				return result;
			}
			const bool stable_active_shape = existing_source
				? post_stage_active.exists && post_stage_active.is_regular_file &&
					same_file_identity(
						initial_active.file_identity, post_stage_active.file_identity)
				: !post_stage_active.exists && !post_stage_active.is_regular_file;
			const bool stable_source_active = stable_active_shape &&
				post_stage_active.canonical_relative_path ==
					initial_active.canonical_relative_path &&
				post_stage_active.resolved_absolute_path ==
					initial_active.resolved_absolute_path &&
				post_stage_active.canonical_identity ==
					initial_active.canonical_identity;
			if (!stable_source_active)
			{
				active_writer.reset();
				if (!cleanup_registry.TrackStageFile(
					created_active_stage, created_active_identity))
				{
					set_failure(VegetationChunkSetPrepareStatus::Failed,
						"Vegetation active target changed and stage ownership registration failed");
					return result;
				}
				data.stage_path = std::move(created_active_stage);
				data.stage_file_identity = created_active_identity;
				data.cleanup_registry = &cleanup_registry;
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active target changed during stage creation");
				return result;
			}
			data.source_active_file_identity = post_stage_active.file_identity;
			if (!cleanup_registry.TrackStageFile(
				created_active_stage, created_active_identity))
			{
				active_writer.reset();
				set_failure(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active stage ownership registration failed");
				return result;
			}
			data.stage_path = std::move(created_active_stage);
			data.stage_file_identity = created_active_identity;
			data.cleanup_registry = &cleanup_registry;

			uint64_t active_offset = 0;
			while (active_offset < active_bytes.size())
			{
				operation_error.clear();
				if (!check_prepare_control(
					control, operation_status, operation_error,
					"before active stage write"))
				{
					fail_with_active_stage(operation_status, std::move(operation_error));
					return result;
				}
				const uint64_t remaining =
					static_cast<uint64_t>(active_bytes.size()) - active_offset;
				const size_t block_size = static_cast<size_t>(
					std::min<uint64_t>(remaining, max_prepare_write_block_bytes));
				if (block_size == 0 || block_size > max_prepare_write_block_bytes ||
					!active_writer->WriteBlock(active_offset,
						{ active_bytes.data() + static_cast<size_t>(active_offset), block_size }))
				{
					fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
						"Vegetation active stage write failed");
					return result;
				}
				active_offset += block_size;
				operation_error.clear();
				if (!check_prepare_control(
					control, operation_status, operation_error,
					"after active stage write"))
				{
					fail_with_active_stage(operation_status, std::move(operation_error));
					return result;
				}
			}
			if (!active_writer->FlushAndClose())
			{
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation active stage flush failed");
				return result;
			}
			active_writer.reset();
			operation_error.clear();
			if (!check_prepare_control(
				control, operation_status, operation_error,
				"after active stage flush"))
			{
				fail_with_active_stage(operation_status, std::move(operation_error));
				return result;
			}

			const VegetationFileBytesResult active_readback = file_ops.ReadAllBytes(
				data.stage_path, static_cast<uint64_t>(active_bytes.size()));
			operation_error.clear();
			if (!check_prepare_control(
				control, operation_status, operation_error,
				"after active stage readback"))
			{
				fail_with_active_stage(operation_status, std::move(operation_error));
				return result;
			}
			VegetationChunkSetActivePointer decoded_active{};
			codec_error.clear();
			if (!valid_bytes_shape(
					active_readback, static_cast<uint64_t>(active_bytes.size())) ||
				active_readback.status != VegetationFileResultStatus::Succeeded ||
				active_readback.bytes != active_bytes ||
				vegetation_sha256(
					active_readback.bytes.data(), active_readback.bytes.size()) !=
					active_sha256 ||
				!decode_vegetation_chunk_set_active_pointer(
					active_readback.bytes, decoded_active, &codec_error) ||
				decoded_active.manifest_sha256 != manifest_sha256)
			{
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					codec_error.empty()
						? "Vegetation active stage strict readback failed"
						: std::move(codec_error));
				return result;
			}

			data.source_active_identity = transaction.source_active_identity;
			data.expected_identity = transaction.expected_identity;
			data.manifest_sha256 = manifest_sha256;
			data.active_stage_size = static_cast<uint64_t>(active_readback.bytes.size());
			data.active_stage_sha256 = active_sha256;
			data.status = VegetationChunkSetPrepareStatus::Prepared;
			data.error.clear();
			return result;
		}
		catch (const std::bad_alloc&)
		{
			if (!data.stage_path.empty())
			{
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation allocation failed");
			}
			else
			{
				fail_with_tree(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation allocation failed");
			}
			return result;
		}
		catch (const std::length_error&)
		{
			if (!data.stage_path.empty())
			{
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation exceeded a container limit");
			}
			else
			{
				fail_with_tree(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation exceeded a container limit");
			}
			return result;
		}
		catch (...)
		{
			if (!data.stage_path.empty())
			{
				fail_with_active_stage(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation failed unexpectedly");
			}
			else
			{
				fail_with_tree(VegetationChunkSetPrepareStatus::Failed,
					"Vegetation chunk-set preparation failed unexpectedly");
			}
			return result;
		}
	}

	VegetationChunkSetCommitResult commit_vegetation_chunk_set(
		const VegetationPreparedChunkSet& prepared,
		const VegetationChunkSetExpectedIdentity& current_identity,
		VegetationOperationControl control,
		VegetationOwnedStageCleanupRegistry& cleanup_registry,
		IVegetationCommitFileOps& file_ops)
	{
		VegetationChunkSetCommitResult result{};
		const auto& data = VegetationChunkSetAccess::Data(prepared);
		std::filesystem::path expected_store{};
		std::filesystem::path expected_active{};
		std::filesystem::path stage_relative{};
		auto fail_without_cleanup = [&](std::string error)
		{
			result.status = VegetationChunkSetCommitStatus::Failed;
			result.error = std::move(error);
			return result;
		};
		auto finish_with_cleanup = [&](const VegetationChunkSetCommitStatus status,
			std::string error)
		{
			result.status = status;
			result.error = std::move(error);
			if (!cleanup_registry.OwnsStageFile(data.stage_path) ||
				!cleanup_registry.CleanupStageFile(data.stage_path, file_ops))
			{
				result.status = VegetationChunkSetCommitStatus::Failed;
				result.error += "; active stage cleanup failed";
			}
			return result;
		};
		auto protect_intact_prepared_stage = [&]()
		{
			try
			{
				if (!cleanup_registry.OwnsStageFile(data.stage_path))
				{
					return false;
				}
				const VegetationFileInspection recovery_stage = file_ops.InspectPath(
					data.asset_root, stage_relative);
				const bool stable_stage_path =
					valid_inspection_shape(recovery_stage) &&
					recovery_stage.status == VegetationFileResultStatus::Succeeded &&
					recovery_stage.canonical_relative_path == stage_relative &&
					recovery_stage.resolved_absolute_path == data.stage_path;
				if (stable_stage_path &&
					(!recovery_stage.exists ||
						(recovery_stage.file_identity.available &&
							!same_file_identity(
								recovery_stage.file_identity,
								data.stage_file_identity))))
				{
					(void)cleanup_registry.
						AbandonStageFileOwnershipAfterIdentityDrift(
							data.stage_path, data.stage_file_identity);
					return false;
				}
				if (!stable_stage_path ||
					!recovery_stage.exists || !recovery_stage.is_regular_file ||
					recovery_stage.canonical_identity == data.layer_canonical_identity ||
					recovery_stage.canonical_identity == data.store_canonical_identity ||
					recovery_stage.canonical_identity == data.active_canonical_identity ||
					!same_file_identity(
						recovery_stage.file_identity, data.stage_file_identity) ||
					same_file_identity(
						recovery_stage.file_identity, data.layer_file_identity) ||
					same_file_identity(
						recovery_stage.file_identity, data.store_file_identity))
				{
					return false;
				}
				const VegetationFileBytesResult recovery_bytes = file_ops.ReadAllBytes(
					data.stage_path, data.active_stage_size);
				VegetationChunkSetActivePointer decoded{};
				std::string codec_error{};
				if (!valid_bytes_shape(recovery_bytes, data.active_stage_size) ||
					recovery_bytes.status != VegetationFileResultStatus::Succeeded ||
					recovery_bytes.bytes.size() != asva_bytes ||
					!decode_vegetation_chunk_set_active_pointer(
						recovery_bytes.bytes, decoded, &codec_error) ||
					vegetation_sha256(recovery_bytes.bytes.data(),
						recovery_bytes.bytes.size()) != data.active_stage_sha256 ||
					decoded.manifest_sha256 != data.manifest_sha256)
				{
					return false;
				}
				if (cleanup_registry.IsRecoveryStageFile(data.stage_path))
				{
					return true;
				}
				return cleanup_registry.RetainStageFileForRecovery(data.stage_path);
			}
			catch (...)
			{
				return false;
			}
		};

		try
		{
			expected_store = data.layer_canonical_relative_path;
			expected_store += L".AshVegetationChunks";
			expected_active = expected_store / L"active.asva";
			stage_relative = (expected_active.parent_path() /
				data.stage_path.filename()).lexically_normal();
		}
		catch (...)
		{
			return fail_without_cleanup(
				"Vegetation chunk-set prepared path derivation failed");
		}

		const bool normalized_paths =
			!data.asset_root.empty() && data.asset_root.is_absolute() &&
			data.asset_root.lexically_normal() == data.asset_root &&
			is_canonical_rootless_relative_path(data.layer_canonical_relative_path) &&
			is_canonical_rootless_relative_path(data.store_canonical_relative_path) &&
			is_canonical_rootless_relative_path(data.active_canonical_relative_path) &&
			is_canonical_rootless_relative_path(stage_relative) &&
			has_layer_extension(data.layer_canonical_relative_path) &&
			data.store_canonical_relative_path == expected_store &&
			data.active_canonical_relative_path == expected_active &&
			data.layer_resolved_absolute_path ==
				(data.asset_root / data.layer_canonical_relative_path).lexically_normal() &&
			data.store_resolved_absolute_path ==
				(data.asset_root / data.store_canonical_relative_path).lexically_normal() &&
			data.active_resolved_absolute_path ==
				(data.asset_root / data.active_canonical_relative_path).lexically_normal() &&
			data.stage_path == (data.asset_root / stage_relative).lexically_normal() &&
			data.stage_path.is_absolute() &&
			data.stage_path.lexically_normal() == data.stage_path &&
			data.stage_path != data.active_resolved_absolute_path &&
			data.stage_path.parent_path() == data.active_resolved_absolute_path.parent_path() &&
			is_strict_lexical_descendant(data.layer_resolved_absolute_path, data.asset_root) &&
			is_strict_lexical_descendant(data.store_resolved_absolute_path, data.asset_root) &&
			is_strict_lexical_descendant(data.active_resolved_absolute_path, data.asset_root) &&
			is_strict_lexical_descendant(data.stage_path, data.asset_root);
		const bool canonical_identities =
			!data.layer_canonical_identity.empty() &&
			!data.store_canonical_identity.empty() &&
			!data.active_canonical_identity.empty() &&
			data.layer_canonical_identity != data.store_canonical_identity &&
			data.layer_canonical_identity != data.active_canonical_identity &&
			data.store_canonical_identity != data.active_canonical_identity;
		const bool native_identities =
			data.layer_file_identity.available && data.store_file_identity.available &&
			data.stage_file_identity.available &&
			!same_file_identity(data.layer_file_identity, data.store_file_identity) &&
			!same_file_identity(data.layer_file_identity, data.stage_file_identity) &&
			!same_file_identity(data.store_file_identity, data.stage_file_identity);
		const bool no_active_source =
			data.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::NoActive &&
			all_zero(data.source_active_identity.manifest_sha256);
		const bool existing_source =
			data.source_active_identity.state ==
				VegetationChunkSetSourceActiveState::Existing &&
			!all_zero(data.source_active_identity.manifest_sha256);
		const bool source_active_native_identity = no_active_source
			? !data.source_active_file_identity.available &&
				data.source_active_file_identity.volume_serial_number == 0 &&
				data.source_active_file_identity.file_index == 0
			: existing_source && data.source_active_file_identity.available &&
				!same_file_identity(
					data.source_active_file_identity, data.layer_file_identity) &&
				!same_file_identity(
					data.source_active_file_identity, data.store_file_identity) &&
				!same_file_identity(
					data.source_active_file_identity, data.stage_file_identity);
		const bool capability_shape =
			data.status == VegetationChunkSetPrepareStatus::Prepared &&
			data.error.empty() && normalized_paths && canonical_identities &&
			native_identities && source_active_native_identity &&
			valid_expected_identity_shape(data.expected_identity) &&
			!all_zero(data.manifest_sha256) && data.active_stage_size == asva_bytes &&
			!all_zero(data.active_stage_sha256) &&
			data.cleanup_registry == &cleanup_registry &&
			cleanup_registry.OwnsStageFile(data.stage_path) &&
			!cleanup_registry.IsRecoveryStageFile(data.stage_path);
		if (!capability_shape)
		{
			return fail_without_cleanup(
				"Vegetation chunk-set prepared commit capability is invalid");
		}

		if (current_identity != data.expected_identity)
		{
			return finish_with_cleanup(VegetationChunkSetCommitStatus::SourceChanged,
				"Vegetation chunk-set expected identity changed before commit");
		}

		PrepareControlState state = prepare_control_state(control);
		if (state != PrepareControlState::Active)
		{
			return finish_with_cleanup(commit_status(state),
				"Vegetation chunk-set commit stopped before lease");
		}

		try
		{
			VegetationFileLeaseResult lease = file_ops.AcquireNamedLease(
				data.store_canonical_identity, control);
			if (!valid_lease_shape(lease))
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation chunk-set lease returned an illegal result shape");
			}
			if (lease.status != VegetationFileLeaseStatus::Acquired)
			{
				const VegetationChunkSetCommitStatus lease_status =
					lease.status == VegetationFileLeaseStatus::Cancelled
						? VegetationChunkSetCommitStatus::Cancelled
						: lease.status == VegetationFileLeaseStatus::TimedOut
							? VegetationChunkSetCommitStatus::TimedOut
							: VegetationChunkSetCommitStatus::Failed;
				return finish_with_cleanup(lease_status,
					lease.error.empty()
						? "Vegetation chunk-set lease acquisition failed"
						: lease.error);
			}

			state = prepare_control_state(control);
			if (state != PrepareControlState::Active)
			{
				return finish_with_cleanup(commit_status(state),
					"Vegetation chunk-set commit stopped after lease acquisition");
			}

			const VegetationFileInspection layer = file_ops.InspectPath(
				data.asset_root, data.layer_canonical_relative_path);
			if (!valid_inspection_shape(layer) ||
				layer.status != VegetationFileResultStatus::Succeeded ||
				!layer.exists || !layer.is_regular_file ||
				layer.canonical_relative_path != data.layer_canonical_relative_path ||
				layer.resolved_absolute_path != data.layer_resolved_absolute_path ||
				layer.canonical_identity != data.layer_canonical_identity ||
				!same_file_identity(layer.file_identity, data.layer_file_identity))
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation Layer identity changed before chunk-set commit");
			}

			const VegetationFileInspection store = file_ops.InspectPath(
				data.asset_root, data.store_canonical_relative_path);
			if (!valid_inspection_shape(store) ||
				store.status != VegetationFileResultStatus::Succeeded ||
				!store.exists || store.is_regular_file ||
				store.canonical_relative_path != data.store_canonical_relative_path ||
				store.resolved_absolute_path != data.store_resolved_absolute_path ||
				store.canonical_identity != data.store_canonical_identity ||
				!same_file_identity(store.file_identity, data.store_file_identity))
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation chunk store identity changed before commit");
			}

			const VegetationFileInspection active = file_ops.InspectPath(
				data.asset_root, data.active_canonical_relative_path);
			if (!valid_inspection_shape(active) ||
				active.status != VegetationFileResultStatus::Succeeded ||
				active.canonical_relative_path != data.active_canonical_relative_path ||
				active.resolved_absolute_path != data.active_resolved_absolute_path ||
				active.canonical_identity != data.active_canonical_identity)
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation active pointer inspection changed before commit");
			}
			if (no_active_source)
			{
				if (active.exists)
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::SourceChanged,
						"Vegetation active pointer appeared before create-new commit");
				}
			}
			else
			{
				if (!active.exists)
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::SourceChanged,
						"Vegetation active pointer disappeared before replace commit");
				}
				if (!active.is_regular_file)
				{
					return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
						"Vegetation active pointer is not a regular file");
				}
				if (!same_file_identity(
						active.file_identity, data.source_active_file_identity))
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::SourceChanged,
						"Vegetation active pointer native identity changed before replace");
				}
				const VegetationFileBytesResult source_active = file_ops.ReadAllBytes(
					active.resolved_absolute_path, asva_bytes);
				if (!valid_bytes_shape(source_active, asva_bytes))
				{
					return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
						"Vegetation active pointer read returned an illegal result shape");
				}
				if (source_active.status == VegetationFileResultStatus::NotFound)
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::SourceChanged,
						"Vegetation active pointer disappeared during replace validation");
				}
				VegetationChunkSetActivePointer decoded_source{};
				std::string source_codec_error{};
				if (source_active.status != VegetationFileResultStatus::Succeeded ||
					source_active.bytes.size() != asva_bytes ||
					!decode_vegetation_chunk_set_active_pointer(
						source_active.bytes, decoded_source, &source_codec_error))
				{
					return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
						source_codec_error.empty()
							? "Vegetation active pointer strict reread failed"
							: std::move(source_codec_error));
				}
				if (decoded_source.manifest_sha256 !=
					data.source_active_identity.manifest_sha256)
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::SourceChanged,
						"Vegetation active manifest identity changed before replace");
				}
			}

			state = prepare_control_state(control);
			if (state != PrepareControlState::Active)
			{
				return finish_with_cleanup(commit_status(state),
					"Vegetation chunk-set commit stopped after active CAS validation");
			}

			const VegetationFileInspection stage = file_ops.InspectPath(
				data.asset_root, stage_relative);
			if (!valid_inspection_shape(stage) ||
				stage.status != VegetationFileResultStatus::Succeeded ||
				!stage.exists || !stage.is_regular_file ||
				stage.canonical_relative_path != stage_relative ||
				stage.resolved_absolute_path != data.stage_path ||
				stage.canonical_identity == data.layer_canonical_identity ||
				stage.canonical_identity == data.store_canonical_identity ||
				stage.canonical_identity == data.active_canonical_identity ||
				!same_file_identity(stage.file_identity, data.stage_file_identity) ||
				same_file_identity(stage.file_identity, data.layer_file_identity) ||
				same_file_identity(stage.file_identity, data.store_file_identity))
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation active stage identity changed before publish");
			}

			const VegetationFileBytesResult staged = file_ops.ReadAllBytes(
				data.stage_path, data.active_stage_size);
			VegetationChunkSetActivePointer decoded{};
			std::string codec_error{};
			if (!valid_bytes_shape(staged, data.active_stage_size) ||
				staged.status != VegetationFileResultStatus::Succeeded ||
				staged.bytes.size() != asva_bytes ||
				!decode_vegetation_chunk_set_active_pointer(
					staged.bytes, decoded, &codec_error) ||
				vegetation_sha256(staged.bytes.data(), staged.bytes.size()) !=
					data.active_stage_sha256 ||
				decoded.manifest_sha256 != data.manifest_sha256)
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					codec_error.empty()
						? "Vegetation active stage bytes changed before publish"
						: std::move(codec_error));
			}

			state = prepare_control_state(control);
			if (state != PrepareControlState::Active)
			{
				return finish_with_cleanup(commit_status(state),
					"Vegetation chunk-set commit stopped before active pointer publish");
			}

			auto published_target_matches_staged_pointer = [&]()
			{
				try
				{
					const VegetationFileInspection stage_after = file_ops.InspectPath(
						data.asset_root, stage_relative);
					if (!valid_inspection_shape(stage_after) ||
						stage_after.status != VegetationFileResultStatus::Succeeded ||
						stage_after.exists || stage_after.is_regular_file ||
						stage_after.canonical_relative_path != stage_relative ||
						stage_after.resolved_absolute_path != data.stage_path)
					{
						return false;
					}

					const VegetationFileInspection target_after = file_ops.InspectPath(
						data.asset_root, data.active_canonical_relative_path);
					if (!valid_inspection_shape(target_after) ||
						target_after.status != VegetationFileResultStatus::Succeeded ||
						!target_after.exists || !target_after.is_regular_file ||
						target_after.canonical_relative_path !=
							data.active_canonical_relative_path ||
						target_after.resolved_absolute_path !=
							data.active_resolved_absolute_path ||
						target_after.canonical_identity != data.active_canonical_identity ||
						!same_file_identity(
							target_after.file_identity, data.stage_file_identity))
					{
						return false;
					}

					const VegetationFileBytesResult target_bytes = file_ops.ReadAllBytes(
						data.active_resolved_absolute_path, data.active_stage_size);
					VegetationChunkSetActivePointer decoded_target{};
					std::string target_codec_error{};
					return valid_bytes_shape(target_bytes, data.active_stage_size) &&
						target_bytes.status == VegetationFileResultStatus::Succeeded &&
						target_bytes.bytes.size() == asva_bytes &&
						vegetation_sha256(target_bytes.bytes.data(),
							target_bytes.bytes.size()) == data.active_stage_sha256 &&
						decode_vegetation_chunk_set_active_pointer(
							target_bytes.bytes, decoded_target, &target_codec_error) &&
						decoded_target.manifest_sha256 == data.manifest_sha256;
				}
				catch (...)
				{
					return false;
				}
			};

			if (no_active_source)
			{
				VegetationCreateNewStatus created = VegetationCreateNewStatus::Failed;
				try
				{
					created = file_ops.CreateNewFromStage(
						data.stage_path, data.active_resolved_absolute_path);
				}
				catch (...)
				{
					if (published_target_matches_staged_pointer())
					{
						if (!cleanup_registry.ForgetConsumedStageFile(data.stage_path))
						{
							(void)cleanup_registry.ReconcileConsumedStageFileAfterPublish(
								data.stage_path, file_ops);
						}
						result.status = VegetationChunkSetCommitStatus::Succeeded;
						result.recovery_path.clear();
						result.error.clear();
						return result;
					}

					result.status = VegetationChunkSetCommitStatus::Failed;
					result.recovery_path.clear();
					result.error =
						"Vegetation active pointer create-new threw with no provable outcome";
					if (cleanup_registry.OwnsStageFile(data.stage_path) &&
						!cleanup_registry.IsRecoveryStageFile(data.stage_path) &&
						!cleanup_registry.CleanupStageFile(data.stage_path, file_ops))
					{
						result.error += "; active stage cleanup failed";
					}
					return result;
				}
				if (created == VegetationCreateNewStatus::Created)
				{
					// Create-new has already consumed the stage. Late cancellation and
					// bookkeeping races cannot roll back or downgrade that publication.
					if (!cleanup_registry.ForgetConsumedStageFile(data.stage_path))
					{
						(void)cleanup_registry.ReconcileConsumedStageFileAfterPublish(
							data.stage_path, file_ops);
					}
					result.status = VegetationChunkSetCommitStatus::Succeeded;
					result.error.clear();
					return result;
				}
				if (created == VegetationCreateNewStatus::AlreadyExists)
				{
					return finish_with_cleanup(
						VegetationChunkSetCommitStatus::AlreadyExists,
						"Vegetation active pointer was won by another create-new commit");
				}
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation active pointer create-new publish failed");
			}
			auto fail_after_atomic_exception = [&]()
			{
				result.status = VegetationChunkSetCommitStatus::Failed;
				result.recovery_path.clear();
				result.error = "Vegetation active pointer atomic replace threw unexpectedly";
				if (cleanup_registry.OwnsStageFile(data.stage_path) &&
					!cleanup_registry.IsRecoveryStageFile(data.stage_path) &&
					!cleanup_registry.CleanupStageFile(data.stage_path, file_ops))
				{
					result.error += "; active stage cleanup failed";
				}
				return result;
			};
			auto valid_distinct_recovery_backup = [&](
				const std::filesystem::path& recovery_path)
			{
				try
				{
					const std::wstring filename = recovery_path.filename().native();
					const std::wstring prefix =
						L".ashveg-layer-stage-replace-backup-";
					const std::wstring suffix = L".tmp";
					if (recovery_path.empty() || !recovery_path.is_absolute() ||
						recovery_path.lexically_normal() != recovery_path ||
						recovery_path == data.stage_path ||
						recovery_path == data.active_resolved_absolute_path ||
						recovery_path.parent_path() !=
							data.active_resolved_absolute_path.parent_path() ||
						filename.size() <= prefix.size() + suffix.size() ||
						filename.compare(0, prefix.size(), prefix) != 0 ||
						filename.compare(
							filename.size() - suffix.size(), suffix.size(), suffix) != 0)
					{
						return false;
					}

					const std::filesystem::path recovery_relative =
						recovery_path.lexically_relative(data.asset_root);
					if (!is_canonical_rootless_relative_path(recovery_relative) ||
						!is_strict_lexical_descendant(recovery_path, data.asset_root))
					{
						return false;
					}
					const VegetationFileInspection recovery = file_ops.InspectPath(
						data.asset_root, recovery_relative);
					if (!valid_inspection_shape(recovery) ||
						recovery.status != VegetationFileResultStatus::Succeeded ||
						!recovery.exists || !recovery.is_regular_file ||
						recovery.canonical_relative_path != recovery_relative ||
						recovery.resolved_absolute_path != recovery_path ||
						!recovery.file_identity.available ||
						same_file_identity(
							recovery.file_identity, data.stage_file_identity) ||
						same_file_identity(
							recovery.file_identity, data.layer_file_identity) ||
						same_file_identity(
							recovery.file_identity, data.store_file_identity))
					{
						return false;
					}
					if (!cleanup_registry.IsAtomicReplaceRecoveryStageFile(
							recovery_path, data.stage_path,
							data.active_resolved_absolute_path,
							recovery.file_identity))
					{
						if (!same_file_identity(
								recovery.file_identity,
								data.source_active_file_identity))
						{
							(void)cleanup_registry.
								AbandonStageFileOwnershipAfterIdentityDrift(
									recovery_path,
									data.source_active_file_identity);
						}
						return false;
					}

					const VegetationFileBytesResult recovery_bytes = file_ops.ReadAllBytes(
						recovery_path, asva_bytes);
					VegetationChunkSetActivePointer decoded_recovery{};
					std::string recovery_codec_error{};
					return valid_bytes_shape(recovery_bytes, asva_bytes) &&
						recovery_bytes.status == VegetationFileResultStatus::Succeeded &&
						recovery_bytes.bytes.size() == asva_bytes &&
						decode_vegetation_chunk_set_active_pointer(
							recovery_bytes.bytes, decoded_recovery,
							&recovery_codec_error) &&
						decoded_recovery.manifest_sha256 ==
							data.source_active_identity.manifest_sha256;
				}
				catch (...)
				{
					return false;
				}
			};

			VegetationAtomicReplaceResult replaced{};
			try
			{
				replaced = file_ops.AtomicReplace(
					data.stage_path, data.active_resolved_absolute_path, cleanup_registry);
			}
			catch (...)
			{
				if (published_target_matches_staged_pointer())
				{
					if (!cleanup_registry.ForgetConsumedStageFile(data.stage_path))
					{
						(void)cleanup_registry.ReconcileConsumedStageFileAfterPublish(
							data.stage_path, file_ops);
					}
					result.status = VegetationChunkSetCommitStatus::Succeeded;
					result.recovery_path.clear();
					result.error.clear();
					return result;
				}
				if (cleanup_registry.IsAtomicReplaceRecoveryStageFile(
						data.stage_path, data.stage_path,
						data.active_resolved_absolute_path,
						data.stage_file_identity) &&
					protect_intact_prepared_stage())
				{
					result.status = VegetationChunkSetCommitStatus::RecoveryRequired;
					result.recovery_path = data.stage_path;
					result.error =
						"Vegetation active pointer atomic replace threw after publication was pinned";
					return result;
				}
				return fail_after_atomic_exception();
			}
			if (!valid_atomic_replace_shape(replaced))
			{
				if (protect_intact_prepared_stage())
				{
					result.status = VegetationChunkSetCommitStatus::RecoveryRequired;
					result.recovery_path = data.stage_path;
					result.error =
						"Vegetation active pointer atomic replace returned an illegal result shape";
					return result;
				}
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					"Vegetation active pointer atomic replace returned an illegal result shape and no fallback could be protected");
			}
			if (replaced.status == VegetationAtomicReplaceStatus::RecoveryRequired)
			{
				const bool recovery_is_prepared_stage =
					replaced.recovery_path == data.stage_path;
				const bool valid_recovery_artifact = recovery_is_prepared_stage
					? cleanup_registry.IsAtomicReplaceRecoveryStageFile(
							replaced.recovery_path, data.stage_path,
							data.active_resolved_absolute_path,
							data.stage_file_identity) &&
						protect_intact_prepared_stage()
					: valid_distinct_recovery_backup(replaced.recovery_path);
				if (!valid_recovery_artifact)
				{
					if (protect_intact_prepared_stage())
					{
						result.status = VegetationChunkSetCommitStatus::RecoveryRequired;
						result.recovery_path = data.stage_path;
						result.error =
							"Vegetation active pointer atomic replace recovery ownership is invalid";
						return result;
					}
					return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
						"Vegetation active pointer atomic replace recovery ownership is invalid and no fallback could be protected");
				}
				result.status = VegetationChunkSetCommitStatus::RecoveryRequired;
				result.recovery_path = replaced.recovery_path;
				result.error = replaced.error;
				return result;
			}
			if (replaced.status == VegetationAtomicReplaceStatus::TargetPreserved)
			{
				return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
					replaced.error.empty()
						? "Vegetation active pointer atomic replace preserved the target"
						: replaced.error);
			}

			// Atomic replace has consumed the stage. Its target mutation is terminal;
			// late cancellation and bookkeeping races cannot downgrade publication.
			if (!cleanup_registry.ForgetConsumedStageFile(data.stage_path))
			{
				(void)cleanup_registry.ReconcileConsumedStageFileAfterPublish(
					data.stage_path, file_ops);
			}
			result.status = VegetationChunkSetCommitStatus::Succeeded;
			result.error.clear();
			return result;
		}
		catch (...)
		{
			return finish_with_cleanup(VegetationChunkSetCommitStatus::Failed,
				"Vegetation chunk-set commit failed unexpectedly");
		}
	}

	bool encode_vegetation_chunk_set_manifest(
		const VegetationChunkSetManifest& manifest,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error)
	{
		out_bytes.clear();
		try
		{
			if (!manifest_is_canonical(manifest))
			{
				return fail(out_error, "Vegetation ASVM manifest is not canonical");
			}
			uint64_t payload_bytes = 0;
			uint64_t total_bytes = 0;
			if (!checked_mul(manifest.entries.size(), asvm_entry_bytes, payload_bytes) ||
				!checked_add(asvm_header_bytes, payload_bytes, total_bytes) ||
				total_bytes > std::vector<uint8_t>{}.max_size())
			{
				return fail(out_error, "Vegetation ASVM size is not representable");
			}

			ByteWriter writer{};
			writer.bytes.reserve(static_cast<size_t>(total_bytes));
			write_magic(writer, { 'A', 'S', 'V', 'M' });
			writer.write_u16(1);
			writer.write_u16(static_cast<uint16_t>(asvm_header_bytes));
			writer.write_array(manifest.layer_id);
			writer.write_u64(manifest.layer_generation);
			writer.write_array(manifest.surface_identity.surface_id);
			writer.write_u64(manifest.surface_identity.content_revision);
			writer.write_u64(manifest.surface_identity.residency_revision);
			writer.write_u64(manifest.surface_identity.transform_revision);
			writer.write_u32(static_cast<uint32_t>(manifest.entries.size()));
			writer.write_u32(0);
			writer.write_u64(payload_bytes);
			writer.write_u32(0);
			writer.write_u32(0);
			for (const VegetationChunkSetManifestEntry& entry : manifest.entries)
			{
				writer.write_i64(entry.coord.x);
				writer.write_i64(entry.coord.z);
				writer.write_array(entry.object_sha256);
				writer.write_array(entry.input_sha256);
			}
			write_u32_at(writer.bytes, 88, vegetation_crc32(
				writer.bytes.data() + asvm_header_bytes,
				writer.bytes.size() - asvm_header_bytes));
			write_u32_at(writer.bytes, 92,
				vegetation_crc32(writer.bytes.data(), asvm_header_bytes));
			out_bytes = std::move(writer.bytes);
			clear_error(out_error);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			out_bytes.clear();
			return fail(out_error, "Vegetation ASVM allocation failed");
		}
		catch (const std::length_error&)
		{
			out_bytes.clear();
			return fail(out_error, "Vegetation ASVM size exceeded a container limit");
		}
	}

	bool decode_vegetation_chunk_set_manifest(
		const std::vector<uint8_t>& bytes,
		const uint32_t max_entries,
		VegetationChunkSetManifest& out_manifest,
		std::string* out_error)
	{
		out_manifest = {};
		try
		{
			ByteReader reader(bytes);
			std::array<uint8_t, 4> magic{};
			uint16_t version = 0;
			uint16_t header_bytes = 0;
			VegetationChunkSetManifest manifest{};
			uint32_t entry_count = 0;
			uint32_t reserved = 0;
			uint64_t payload_bytes = 0;
			uint32_t payload_crc = 0;
			uint32_t header_crc = 0;
			if (!reader.read_array(magic) || !reader.read_u16(version) ||
				!reader.read_u16(header_bytes) || !reader.read_array(manifest.layer_id) ||
				!reader.read_u64(manifest.layer_generation) ||
				!reader.read_array(manifest.surface_identity.surface_id) ||
				!reader.read_u64(manifest.surface_identity.content_revision) ||
				!reader.read_u64(manifest.surface_identity.residency_revision) ||
				!reader.read_u64(manifest.surface_identity.transform_revision) ||
				!reader.read_u32(entry_count) || !reader.read_u32(reserved) ||
				!reader.read_u64(payload_bytes) || !reader.read_u32(payload_crc) ||
				!reader.read_u32(header_crc))
			{
				return fail(out_error, "Vegetation ASVM header is truncated");
			}
			uint64_t expected_payload_bytes = 0;
			uint64_t expected_total_bytes = 0;
			if (magic != std::array<uint8_t, 4>{ 'A', 'S', 'V', 'M' } ||
				version != 1 || header_bytes != asvm_header_bytes || reserved != 0 ||
				all_zero(manifest.layer_id) || manifest.layer_generation == 0 ||
				all_zero(manifest.surface_identity.surface_id) ||
				entry_count > max_entries ||
				!checked_mul(entry_count, asvm_entry_bytes, expected_payload_bytes) ||
				payload_bytes != expected_payload_bytes ||
				!checked_add(asvm_header_bytes, payload_bytes, expected_total_bytes) ||
				expected_total_bytes != bytes.size() ||
				!header_crc_matches<asvm_header_bytes>(bytes, 92, header_crc) ||
				vegetation_crc32(bytes.data() + asvm_header_bytes,
					static_cast<size_t>(payload_bytes)) != payload_crc)
			{
				return fail(out_error, "Vegetation ASVM header, size, budget, or CRC is invalid");
			}

			manifest.entries.reserve(entry_count);
			for (uint32_t index = 0; index < entry_count; ++index)
			{
				VegetationChunkSetManifestEntry entry{};
				if (!reader.read_i64(entry.coord.x) || !reader.read_i64(entry.coord.z) ||
					!reader.read_array(entry.object_sha256) ||
					!reader.read_array(entry.input_sha256) ||
					all_zero(entry.object_sha256) || all_zero(entry.input_sha256) ||
					(!manifest.entries.empty() && !manifest_coord_less(
						manifest.entries.back().coord, entry.coord)))
				{
					return fail(out_error, "Vegetation ASVM entry is invalid or unsorted");
				}
				manifest.entries.push_back(entry);
			}
			if (!reader.at_end())
			{
				return fail(out_error, "Vegetation ASVM payload has a tail");
			}
			out_manifest = std::move(manifest);
			clear_error(out_error);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			out_manifest = {};
			return fail(out_error, "Vegetation ASVM allocation failed");
		}
		catch (const std::length_error&)
		{
			out_manifest = {};
			return fail(out_error, "Vegetation ASVM size exceeded a container limit");
		}
	}

	bool encode_vegetation_chunk_set_active_pointer(
		const VegetationChunkSetActivePointer& pointer,
		std::vector<uint8_t>& out_bytes,
		std::string* out_error)
	{
		out_bytes.clear();
		try
		{
			if (all_zero(pointer.manifest_sha256))
			{
				return fail(out_error, "Vegetation ASVA manifest digest is invalid");
			}
			ByteWriter writer{};
			writer.bytes.reserve(asva_bytes);
			write_magic(writer, { 'A', 'S', 'V', 'A' });
			writer.write_u16(1);
			writer.write_u16(static_cast<uint16_t>(asva_bytes));
			writer.write_array(pointer.manifest_sha256);
			writer.write_u32(0);
			writer.write_u32(0);
			write_u32_at(writer.bytes, 44, vegetation_crc32(writer.bytes.data(), 44));
			out_bytes = std::move(writer.bytes);
			clear_error(out_error);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			out_bytes.clear();
			return fail(out_error, "Vegetation ASVA allocation failed");
		}
		catch (const std::length_error&)
		{
			out_bytes.clear();
			return fail(out_error, "Vegetation ASVA size exceeded a container limit");
		}
	}

	bool decode_vegetation_chunk_set_active_pointer(
		const std::vector<uint8_t>& bytes,
		VegetationChunkSetActivePointer& out_pointer,
		std::string* out_error)
	{
		out_pointer = {};
		try
		{
			ByteReader reader(bytes);
			std::array<uint8_t, 4> magic{};
			uint16_t version = 0;
			uint16_t header_bytes = 0;
			VegetationChunkSetActivePointer pointer{};
			uint32_t reserved = 0;
			uint32_t crc = 0;
			if (bytes.size() != asva_bytes || !reader.read_array(magic) ||
				!reader.read_u16(version) || !reader.read_u16(header_bytes) ||
				!reader.read_array(pointer.manifest_sha256) ||
				!reader.read_u32(reserved) || !reader.read_u32(crc) || !reader.at_end() ||
				magic != std::array<uint8_t, 4>{ 'A', 'S', 'V', 'A' } ||
				version != 1 || header_bytes != asva_bytes || reserved != 0 ||
				all_zero(pointer.manifest_sha256) ||
				vegetation_crc32(bytes.data(), 44) != crc)
			{
				return fail(out_error, "Vegetation ASVA shape, header, or CRC is invalid");
			}
			out_pointer = pointer;
			clear_error(out_error);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			out_pointer = {};
			return fail(out_error, "Vegetation ASVA allocation failed");
		}
		catch (const std::length_error&)
		{
			out_pointer = {};
			return fail(out_error, "Vegetation ASVA size exceeded a container limit");
		}
	}

	VegetationActiveChunkSetReadResult read_active_vegetation_chunk_set(
		const std::filesystem::path& asset_root,
		const std::filesystem::path& layer_path,
		const VegetationAssetResolverSnapshot& resolver,
		const VegetationChunkSetLoadBudget& budget,
		VegetationOperationControl control,
		IVegetationStageFileOps& file_ops)
	{
		VegetationActiveChunkSetReadResult result{};
		try
		{
			if (!check_control(control, result, "entry"))
			{
				return result;
			}
			if (!is_canonical_rootless_relative_path(layer_path) ||
				!has_layer_extension(layer_path) ||
				budget.max_summary_bytes < empty_snapshot_summary_bytes)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation chunk-set path or summary budget is invalid";
				return result;
			}
			std::error_code absolute_error{};
			const std::filesystem::path resolved_asset_root =
				std::filesystem::absolute(asset_root, absolute_error).lexically_normal();
			if (absolute_error || resolved_asset_root.empty() ||
				!resolved_asset_root.is_absolute())
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation chunk-set asset root is invalid";
				return result;
			}

			result.store_relative_path = layer_path;
			result.store_relative_path += L".AshVegetationChunks";
			result.active_relative_path = result.store_relative_path / L"active.asva";
			if (!check_control(control, result, "before active inspection"))
			{
				return result;
			}
			const VegetationFileInspection active_inspection =
				file_ops.InspectPath(resolved_asset_root, result.active_relative_path);
			if (!check_control(control, result, "after active inspection"))
			{
				return result;
			}
			if (!valid_inspection_binding(
				active_inspection, resolved_asset_root, result.active_relative_path))
			{
				result.status = VegetationActiveChunkSetReadStatus::Failed;
				result.error =
					"Vegetation chunk-set active inspection returned an illegal shape or path binding";
				return result;
			}
			if (active_inspection.status == VegetationFileResultStatus::Succeeded &&
				!active_inspection.exists)
			{
				result.status = VegetationActiveChunkSetReadStatus::NoActive;
				return result;
			}
			if (active_inspection.status != VegetationFileResultStatus::Succeeded)
			{
				set_file_failure(result, active_inspection.status,
					active_inspection.error, "Vegetation active inspection failed");
				return result;
			}
			if (!active_inspection.is_regular_file)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active pointer is not a regular file";
				return result;
			}

			uint64_t total_inspected_bytes = 0;
			auto read_snapshot = [&](const VegetationFileInspection& inspection,
				const char* before_boundary, const char* after_boundary,
				VegetationFileBytesResult& out_bytes) -> bool
			{
				if (!check_control(control, result, before_boundary))
				{
					return false;
				}
				const uint64_t remaining = budget.max_total_inspected_bytes -
					total_inspected_bytes;
				const uint64_t ceiling = std::min(
					budget.per_file.max_file_bytes, remaining);
				out_bytes = file_ops.ReadAllBytes(
					inspection.resolved_absolute_path, ceiling);
				if (!check_control(control, result, after_boundary))
				{
					return false;
				}
				if (!valid_bytes_shape(out_bytes, ceiling))
				{
					result.status = VegetationActiveChunkSetReadStatus::Failed;
					result.error = "Vegetation bounded read returned an illegal status-payload shape";
					return false;
				}
				if (out_bytes.status != VegetationFileResultStatus::Succeeded)
				{
					set_file_failure(result, out_bytes.status, out_bytes.error,
						"Vegetation bounded read failed");
					return false;
				}
				uint64_t accumulated = 0;
				if (!checked_add(total_inspected_bytes,
					out_bytes.bytes.size(), accumulated) ||
					accumulated > budget.max_total_inspected_bytes)
				{
					result.status = VegetationActiveChunkSetReadStatus::Failed;
					result.error = "Vegetation bounded read accounting overflowed";
					return false;
				}
				total_inspected_bytes = accumulated;
				return true;
			};

			VegetationFileBytesResult active_bytes{};
			if (!read_snapshot(active_inspection, "before active read",
				"after active read", active_bytes))
			{
				return result;
			}
			if (!check_control(control, result, "before active decode"))
			{
				return result;
			}
			VegetationChunkSetActivePointer pointer{};
			std::string decode_error{};
			const bool active_decoded = decode_vegetation_chunk_set_active_pointer(
				active_bytes.bytes, pointer, &decode_error);
			if (!check_control(control, result, "after active decode"))
			{
				return result;
			}
			if (!active_decoded)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = decode_error.empty()
					? "Vegetation active pointer is invalid" : std::move(decode_error);
				return result;
			}

			const std::filesystem::path manifest_relative_path =
				result.store_relative_path / L"manifests" /
				(lowercase_digest_hex(pointer.manifest_sha256) + ".asvm");
			if (!check_control(control, result, "before manifest inspection"))
			{
				return result;
			}
			const VegetationFileInspection manifest_inspection =
				file_ops.InspectPath(resolved_asset_root, manifest_relative_path);
			if (!check_control(control, result, "after manifest inspection"))
			{
				return result;
			}
			if (!valid_inspection_binding(
				manifest_inspection, resolved_asset_root, manifest_relative_path))
			{
				result.status = VegetationActiveChunkSetReadStatus::Failed;
				result.error =
					"Vegetation manifest inspection returned an illegal shape or path binding";
				return result;
			}
			if (manifest_inspection.status == VegetationFileResultStatus::Succeeded &&
				!manifest_inspection.exists)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active manifest is missing";
				return result;
			}
			if (manifest_inspection.status != VegetationFileResultStatus::Succeeded)
			{
				set_file_failure(result, manifest_inspection.status,
					manifest_inspection.error, "Vegetation manifest inspection failed");
				return result;
			}
			if (!manifest_inspection.is_regular_file)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active manifest is not a regular file";
				return result;
			}

			VegetationFileBytesResult manifest_bytes{};
			if (!read_snapshot(manifest_inspection, "before manifest read",
				"after manifest read", manifest_bytes))
			{
				return result;
			}
			const VegetationSha256 manifest_sha256 = vegetation_sha256(
				manifest_bytes.bytes.data(), manifest_bytes.bytes.size());
			if (!check_control(control, result, "after manifest hash"))
			{
				return result;
			}
			if (manifest_sha256 != pointer.manifest_sha256)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active manifest digest does not match its pointer";
				return result;
			}
			if (manifest_bytes.bytes.size() < asvm_header_bytes)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active manifest header is truncated";
				return result;
			}
			const uint32_t untrusted_entry_count =
				read_u32_little_endian(manifest_bytes.bytes, 72);
			uint64_t entry_summary_bytes = 0;
			uint64_t summary_bytes = 0;
			if (!checked_mul(untrusted_entry_count, snapshot_entry_summary_bytes,
				entry_summary_bytes) ||
				!checked_add(empty_snapshot_summary_bytes, entry_summary_bytes,
					summary_bytes) || summary_bytes > budget.max_summary_bytes)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = "Vegetation active manifest exceeds the summary budget";
				return result;
			}

			if (!check_control(control, result, "before manifest decode"))
			{
				return result;
			}
			VegetationChunkSetManifest manifest{};
			decode_error.clear();
			const bool manifest_decoded = decode_vegetation_chunk_set_manifest(
				manifest_bytes.bytes, budget.max_manifest_entries, manifest, &decode_error);
			if (!check_control(control, result, "after manifest decode"))
			{
				return result;
			}
			if (!manifest_decoded)
			{
				result.status = VegetationActiveChunkSetReadStatus::Invalid;
				result.error = decode_error.empty()
					? "Vegetation active manifest is invalid" : std::move(decode_error);
				return result;
			}
			std::vector<VegetationActiveChunkSetEntrySummary> entry_summaries{};
			entry_summaries.reserve(manifest.entries.size());
			for (const VegetationChunkSetManifestEntry& manifest_entry : manifest.entries)
			{
				const std::filesystem::path object_relative_path =
					result.store_relative_path / L"objects" /
					(lowercase_digest_hex(manifest_entry.object_sha256) +
						".AshVegetationChunk");
				if (!check_control(control, result, "before object inspection"))
				{
					return result;
				}
				const VegetationFileInspection object_inspection =
					file_ops.InspectPath(resolved_asset_root, object_relative_path);
				if (!check_control(control, result, "after object inspection"))
				{
					return result;
				}
				if (!valid_inspection_binding(
					object_inspection, resolved_asset_root, object_relative_path))
				{
					result.status = VegetationActiveChunkSetReadStatus::Failed;
					result.error =
						"Vegetation object inspection returned an illegal shape or path binding";
					return result;
				}
				if (object_inspection.status == VegetationFileResultStatus::Succeeded &&
					!object_inspection.exists)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = "Vegetation manifest object is missing";
					return result;
				}
				if (object_inspection.status != VegetationFileResultStatus::Succeeded)
				{
					set_file_failure(result, object_inspection.status,
						object_inspection.error, "Vegetation object inspection failed");
					return result;
				}
				if (!object_inspection.is_regular_file)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = "Vegetation manifest object is not a regular file";
					return result;
				}

				VegetationFileBytesResult object_bytes{};
				if (!read_snapshot(object_inspection, "before object read",
					"after object read", object_bytes))
				{
					return result;
				}
				const VegetationSha256 object_sha256 = vegetation_sha256(
					object_bytes.bytes.data(), object_bytes.bytes.size());
				if (!check_control(control, result, "after object hash"))
				{
					return result;
				}
				if (object_sha256 != manifest_entry.object_sha256)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = "Vegetation manifest object digest does not match its path";
					return result;
				}

				if (!check_control(control, result, "before object decode"))
				{
					return result;
				}
				VegetationChunk chunk{};
				decode_error.clear();
				const bool chunk_decoded = decode_vegetation_chunk(
					object_bytes.bytes, budget.per_file, chunk, &decode_error);
				if (!check_control(control, result, "after object decode"))
				{
					return result;
				}
				if (!chunk_decoded)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = decode_error.empty()
						? "Vegetation manifest object is invalid" : std::move(decode_error);
					return result;
				}
				if (chunk.chunk.x != manifest_entry.coord.x ||
					chunk.chunk.z != manifest_entry.coord.z ||
					chunk.layer_id != manifest.layer_id ||
					chunk.surface_identity.surface_id != manifest.surface_identity.surface_id ||
					chunk.surface_identity.content_revision !=
						manifest.surface_identity.content_revision ||
					chunk.surface_identity.residency_revision !=
						manifest.surface_identity.residency_revision ||
					chunk.surface_identity.transform_revision !=
						manifest.surface_identity.transform_revision ||
					chunk.chunk_input_sha256 != manifest_entry.input_sha256)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = "Vegetation manifest and object identities do not match";
					return result;
				}

				uint64_t species_summary_bytes = 0;
				uint64_t accumulated_summary_bytes = 0;
				if (!checked_mul(chunk.species.size(), snapshot_species_id_summary_bytes,
					species_summary_bytes) ||
					!checked_add(summary_bytes, species_summary_bytes,
						accumulated_summary_bytes) ||
					accumulated_summary_bytes > budget.max_summary_bytes)
				{
					result.status = VegetationActiveChunkSetReadStatus::Invalid;
					result.error = "Vegetation object exceeds the summary budget";
					return result;
				}
				summary_bytes = accumulated_summary_bytes;

				VegetationActiveChunkSetEntrySummary entry_summary{};
				entry_summary.coord = manifest_entry.coord;
				entry_summary.object_sha256 = manifest_entry.object_sha256;
				entry_summary.input_sha256 = manifest_entry.input_sha256;
				entry_summary.referenced_species_ids.reserve(chunk.species.size());
				std::vector<std::shared_ptr<const VegetationSpecies>> resolved_species{};
				resolved_species.reserve(chunk.species.size());
				for (const VegetationPaletteEntry& palette : chunk.species)
				{
					if (!check_control(control, result, "before species resolution"))
					{
						return result;
					}
					const VegetationAssetLoadResult<VegetationSpecies> loaded =
						resolver.load_species_by_path(
							palette.species_asset_path, budget.per_file);
					if (!check_control(control, result, "after species resolution"))
					{
						return result;
					}

					switch (loaded.failure)
					{
					case VegetationAssetLoadFailure::None:
						if (loaded.state != AssetLoadState::Loaded || !loaded.asset)
						{
							result.status = VegetationActiveChunkSetReadStatus::Failed;
							result.error = "Vegetation Species resolver returned an illegal success shape";
							return result;
						}
						break;
					case VegetationAssetLoadFailure::Io:
						if (loaded.state != AssetLoadState::Failed || loaded.asset)
						{
							result.status = VegetationActiveChunkSetReadStatus::Failed;
							result.error = "Vegetation Species resolver returned an illegal failure shape";
							return result;
						}
						result.status = VegetationActiveChunkSetReadStatus::Failed;
						result.error = loaded.error.empty()
							? "Vegetation Species dependency I/O failed" : loaded.error;
						return result;
					case VegetationAssetLoadFailure::BudgetExceeded:
					case VegetationAssetLoadFailure::WrongType:
					case VegetationAssetLoadFailure::InvalidData:
						if (loaded.state != AssetLoadState::Failed || loaded.asset)
						{
							result.status = VegetationActiveChunkSetReadStatus::Failed;
							result.error = "Vegetation Species resolver returned an illegal failure shape";
							return result;
						}
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = loaded.error.empty()
							? "Vegetation Species dependency is invalid" : loaded.error;
						return result;
					case VegetationAssetLoadFailure::Missing:
						if (loaded.state != AssetLoadState::Missing || loaded.asset)
						{
							result.status = VegetationActiveChunkSetReadStatus::Failed;
							result.error = "Vegetation Species resolver returned an illegal missing shape";
							return result;
						}
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = loaded.error.empty()
							? "Vegetation Species dependency is missing" : loaded.error;
						return result;
					default:
						result.status = VegetationActiveChunkSetReadStatus::Failed;
						result.error = "Vegetation Species resolver returned an unknown failure code";
						return result;
					}

					if (loaded.asset->species_id != palette.species_id)
					{
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = "Vegetation Species id does not match the object palette";
						return result;
					}
					std::vector<uint8_t> canonical_species{};
					std::string encode_error{};
					if (!encode_vegetation_species(
						*loaded.asset, canonical_species, &encode_error))
					{
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = encode_error.empty()
							? "Vegetation Species could not be canonicalized" : std::move(encode_error);
						return result;
					}
					const VegetationSha256 species_sha256 = vegetation_sha256(
						canonical_species.data(), canonical_species.size());
					if (species_sha256 != palette.species_sha256)
					{
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = "Vegetation Species digest does not match the object palette";
						return result;
					}
					entry_summary.referenced_species_ids.push_back(palette.species_id);
					resolved_species.push_back(loaded.asset);
				}

				for (const VegetationChunkInstance& instance : chunk.instances)
				{
					if (instance.species_index >= resolved_species.size() ||
						instance.candidate_ordinal >= resolved_species[
							instance.species_index]->placement.candidates_per_cell)
					{
						result.status = VegetationActiveChunkSetReadStatus::Invalid;
						result.error = "Vegetation object candidate ordinal exceeds the resolved Species contract";
						return result;
					}
				}
				std::sort(entry_summary.referenced_species_ids.begin(),
					entry_summary.referenced_species_ids.end());
				entry_summaries.push_back(std::move(entry_summary));
			}

			auto snapshot = std::make_shared<VegetationActiveChunkSetSnapshot>();
			snapshot->layer_id = manifest.layer_id;
			snapshot->layer_generation = manifest.layer_generation;
			snapshot->surface_identity = manifest.surface_identity;
			snapshot->manifest_sha256 = pointer.manifest_sha256;
			snapshot->entries = std::move(entry_summaries);
			if (!check_control(control, result, "before snapshot publication"))
			{
				return result;
			}
			result.snapshot = std::move(snapshot);
			result.status = VegetationActiveChunkSetReadStatus::Succeeded;
			result.error.clear();
			return result;
		}
		catch (const std::bad_alloc&)
		{
			result.snapshot.reset();
			result.status = VegetationActiveChunkSetReadStatus::Failed;
			result.error = "Vegetation active chunk-set allocation failed";
			return result;
		}
		catch (const std::length_error&)
		{
			result.snapshot.reset();
			result.status = VegetationActiveChunkSetReadStatus::Failed;
			result.error = "Vegetation active chunk-set size exceeded a container limit";
			return result;
		}
		catch (const std::exception& exception)
		{
			VegetationActiveChunkSetReadResult failed{};
			failed.status = VegetationActiveChunkSetReadStatus::Failed;
			failed.error =
				std::string("Vegetation active chunk-set dependency threw: ") +
				exception.what();
			return failed;
		}
		catch (...)
		{
			VegetationActiveChunkSetReadResult failed{};
			failed.status = VegetationActiveChunkSetReadStatus::Failed;
			failed.error =
				"Vegetation active chunk-set dependency threw an unknown exception";
			return failed;
		}
	}
}
