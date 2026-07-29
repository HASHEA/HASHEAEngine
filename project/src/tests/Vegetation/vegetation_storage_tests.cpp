#include "Function/Asset/VegetationFileOps.h"
#include "Function/Asset/VegetationStorage.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
	class ScriptedVegetationFileOps final : public AshEngine::IVegetationStageFileOps
	{
	public:
		explicit ScriptedVegetationFileOps(AshEngine::IVegetationStageFileOps& backing)
			: m_backing(backing)
		{
		}

		std::filesystem::path failing_stage_file{};
		size_t remaining_remove_failures = 0;
		std::vector<std::filesystem::path> removed_stage_files{};
		std::vector<std::filesystem::path> removed_stage_trees{};

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			return m_backing.InspectPath(asset_root, path);
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			return m_backing.ReadAllBytes(path, max_bytes);
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			return m_backing.EnsureDirectoryTree(asset_root, relative_directory);
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			return m_backing.CreateUniqueSiblingStageFile(target, operation_serial);
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			return m_backing.CreateUniqueStageTree(store_root, operation_serial);
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			return m_backing.CreateOwnedStageFile(owned_stage_root, relative_path);
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			removed_stage_files.push_back(stage_file);
			if (stage_file == failing_stage_file && remaining_remove_failures > 0)
			{
				--remaining_remove_failures;
				return false;
			}
			return m_backing.RemoveOwnedStageFile(stage_file, expected_identity);
		}

		bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			removed_stage_trees.push_back(stage_root);
			return m_backing.RemoveOwnedStageTree(stage_root, expected_identity);
		}

	private:
		AshEngine::IVegetationStageFileOps& m_backing;
	};

	class TestVegetationFileLease final : public AshEngine::IVegetationFileLease
	{
	};

	class RecordingStageWriter final : public AshEngine::IVegetationStageFileWriter
	{
	public:
		explicit RecordingStageWriter(std::shared_ptr<bool> used)
			: m_used(std::move(used))
		{
		}

		bool WriteBlock(uint64_t, AshEngine::VegetationByteSpan) override
		{
			*m_used = true;
			return false;
		}

		bool FlushAndClose() override
		{
			*m_used = true;
			return false;
		}

	private:
		std::shared_ptr<bool> m_used{};
	};

	class AliasWritingStageWriter final : public AshEngine::IVegetationStageFileWriter
	{
	public:
		AliasWritingStageWriter(std::filesystem::path path, std::shared_ptr<bool> used)
			: m_path(std::move(path)), m_used(std::move(used))
		{
		}

		bool WriteBlock(const uint64_t offset, const AshEngine::VegetationByteSpan bytes) override
		{
			if (m_closed || bytes.data == nullptr || bytes.size == 0 || offset != m_next_offset)
			{
				return false;
			}
			if (!m_stream.is_open())
			{
				m_stream.open(m_path, std::ios::binary | std::ios::trunc);
			}
			if (!m_stream)
			{
				return false;
			}
			*m_used = true;
			m_stream.write(reinterpret_cast<const char*>(bytes.data),
				static_cast<std::streamsize>(bytes.size));
			if (!m_stream)
			{
				return false;
			}
			m_next_offset += static_cast<uint64_t>(bytes.size);
			return true;
		}

		bool FlushAndClose() override
		{
			if (m_closed || !m_stream.is_open() || m_next_offset == 0)
			{
				return false;
			}
			m_stream.flush();
			const bool flushed = m_stream.good();
			m_stream.close();
			m_closed = true;
			return flushed;
		}

	private:
		std::filesystem::path m_path{};
		std::shared_ptr<bool> m_used{};
		std::ofstream m_stream{};
		uint64_t m_next_offset = 0;
		bool m_closed = false;
	};

	struct ScriptedStageWriterState
	{
		size_t write_calls = 0;
		size_t flush_calls = 0;
		bool fail_write = false;
		bool fail_flush = false;
		std::function<void()> after_write{};
	};

	class ScriptedStageWriter final : public AshEngine::IVegetationStageFileWriter
	{
	public:
		ScriptedStageWriter(
			std::unique_ptr<AshEngine::IVegetationStageFileWriter> backing,
			std::shared_ptr<ScriptedStageWriterState> state)
			: m_backing(std::move(backing)), m_state(std::move(state))
		{
		}

		bool WriteBlock(const uint64_t offset, const AshEngine::VegetationByteSpan bytes) override
		{
			++m_state->write_calls;
			const bool written = !m_state->fail_write && m_backing->WriteBlock(offset, bytes);
			if (m_state->after_write)
			{
				m_state->after_write();
			}
			return written;
		}

		bool FlushAndClose() override
		{
			++m_state->flush_calls;
			return !m_state->fail_flush && m_backing->FlushAndClose();
		}

	private:
		std::unique_ptr<AshEngine::IVegetationStageFileWriter> m_backing{};
		std::shared_ptr<ScriptedStageWriterState> m_state{};
	};

	class ScriptedCommitFileOps final : public AshEngine::IVegetationCommitFileOps
	{
	public:
		explicit ScriptedCommitFileOps(AshEngine::IVegetationFileOps& backing)
			: m_backing(backing)
		{
		}

		std::function<AshEngine::VegetationFileInspection(
			const std::filesystem::path&, const std::filesystem::path&)> inspect{};
		std::function<AshEngine::VegetationFileBytesResult(
			const std::filesystem::path&, uint64_t)> read{};
		std::function<AshEngine::VegetationStageFileResult(
			const std::filesystem::path&, uint64_t)> create_stage{};
		std::function<AshEngine::VegetationFileResultStatus(
			const std::filesystem::path&, const std::filesystem::path&)> ensure_directory{};
		std::function<bool(
			const std::filesystem::path&,
			const AshEngine::VegetationFileIdentity&)> remove_stage{};
		std::function<AshEngine::VegetationFileLeaseResult(
			std::string_view, const AshEngine::VegetationOperationControl&)> acquire{};
		std::function<AshEngine::VegetationAtomicReplaceResult(
			const std::filesystem::path&, const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)> replace{};
		std::function<AshEngine::VegetationCreateNewStatus(
			const std::filesystem::path&, const std::filesystem::path&)> create_new{};

		size_t inspect_call_count = 0;
		size_t read_call_count = 0;
		size_t ensure_directory_call_count = 0;
		size_t remove_stage_call_count = 0;
		size_t acquire_call_count = 0;
		size_t replace_call_count = 0;
		size_t create_new_call_count = 0;
		std::filesystem::path last_created_stage{};
		AshEngine::VegetationFileIdentity last_created_stage_identity{};
		std::string last_lease_identity{};
		std::shared_ptr<const std::atomic_bool> last_lease_cancel_requested{};
		std::chrono::steady_clock::time_point last_lease_deadline{};

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			++inspect_call_count;
			return inspect ? inspect(asset_root, path) : m_backing.InspectPath(asset_root, path);
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			++read_call_count;
			return read ? read(path, max_bytes) : m_backing.ReadAllBytes(path, max_bytes);
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			++ensure_directory_call_count;
			return ensure_directory ? ensure_directory(asset_root, relative_directory)
				: m_backing.EnsureDirectoryTree(asset_root, relative_directory);
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			AshEngine::VegetationStageFileResult result = create_stage
				? create_stage(target, operation_serial)
				: m_backing.CreateUniqueSiblingStageFile(target, operation_serial);
			last_created_stage = result.owned_stage_file;
			last_created_stage_identity = result.file_identity;
			return result;
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			return m_backing.CreateUniqueStageTree(store_root, operation_serial);
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			return m_backing.CreateOwnedStageFile(owned_stage_root, relative_path);
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			++remove_stage_call_count;
			return remove_stage ? remove_stage(stage_file, expected_identity)
				: m_backing.RemoveOwnedStageFile(stage_file, expected_identity);
		}

		bool RemoveOwnedStageTree(
			const std::filesystem::path& stage_root,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			return m_backing.RemoveOwnedStageTree(stage_root, expected_identity);
		}

		AshEngine::VegetationFileLeaseResult AcquireNamedLease(
			const std::string_view canonical_identity,
			const AshEngine::VegetationOperationControl& control) override
		{
			++acquire_call_count;
			last_lease_identity = canonical_identity;
			last_lease_cancel_requested = control.cancel_requested;
			last_lease_deadline = control.deadline;
			return acquire ? acquire(canonical_identity, control)
				: m_backing.AcquireNamedLease(canonical_identity, control);
		}

		AshEngine::VegetationAtomicReplaceResult AtomicReplace(
			const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry) override
		{
			++replace_call_count;
			return replace ? replace(stage, target, cleanup_registry)
				: m_backing.AtomicReplace(stage, target, cleanup_registry);
		}

		AshEngine::VegetationCreateNewStatus CreateNewFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) override
		{
			++create_new_call_count;
			return create_new ? create_new(stage, target)
				: m_backing.CreateNewFromStage(stage, target);
		}

	private:
		AshEngine::IVegetationFileOps& m_backing;
	};

	size_t CountPath(
		const std::vector<std::filesystem::path>& paths,
		const std::filesystem::path& expected)
	{
		return static_cast<size_t>(std::count(paths.begin(), paths.end(), expected));
	}

	AshEngine::VegetationFileIdentity TestFileIdentity(const uint64_t file_index)
	{
		AshEngine::VegetationFileIdentity identity{};
		identity.available = true;
		identity.volume_serial_number = 0x1234u;
		identity.file_index = file_index;
		return identity;
	}

	std::vector<uint8_t> EncodeLayerOrThrow(
		const AshEngine::VegetationLayerSnapshot& snapshot)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(snapshot, bytes, &error))
		{
			throw std::runtime_error("Vegetation storage test layer did not encode: " + error);
		}
		return bytes;
	}

	enum class CreateNewStorageApi : uint8_t
	{
		FirstSave = 0,
		CopyAs
	};

	const char* CreateNewStorageApiName(const CreateNewStorageApi api)
	{
		return api == CreateNewStorageApi::FirstSave ? "first-save" : "copy-as";
	}

	AshEngine::VegetationPreparedLayerWrite PrepareCreateNewStorageWrite(
		const CreateNewStorageApi api,
		const std::filesystem::path& asset_root,
		const std::filesystem::path& target,
		const AshEngine::VegetationLayerSnapshot& snapshot,
		const uint64_t operation_serial,
		AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry)
	{
		if (api == CreateNewStorageApi::FirstSave)
		{
			return AshEngine::prepare_vegetation_layer_write(
				asset_root, target, std::nullopt, snapshot, operation_serial,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry);
		}
		return AshEngine::prepare_vegetation_layer_copy_as(
			asset_root, target, snapshot, operation_serial,
			VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	}

	AshEngine::VegetationStorageResult CommitCreateNewStorageWrite(
		const CreateNewStorageApi api,
		const AshEngine::VegetationPreparedLayerWrite& prepared,
		const uint64_t operation_serial,
		AshEngine::VegetationOwnedStageCleanupRegistry& cleanup_registry,
		AshEngine::IVegetationCommitFileOps& file_ops)
	{
		if (api == CreateNewStorageApi::FirstSave)
		{
			return AshEngine::commit_vegetation_layer_write(
				prepared, operation_serial,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, file_ops);
		}
		return AshEngine::commit_vegetation_layer_copy_as(
			prepared, operation_serial,
			VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, file_ops);
	}

#if defined(_WIN32)
	std::wstring ExtendedWindowsPath(const std::filesystem::path& path)
	{
		const std::wstring value = path.native();
		if (value.rfind(L"\\\\?\\", 0) == 0)
		{
			return value;
		}
		if (value.rfind(L"\\\\", 0) == 0)
		{
			return L"\\\\?\\UNC\\" + value.substr(2);
		}
		return L"\\\\?\\" + value;
	}
#endif
}

TEST_CASE("Vegetation storage FileOps returns legal path and bounded-read shapes")
{
	VegetationTest::ScopedAssetRoot root("storage-file-shapes");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path file_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path file_absolute = root.Path() / file_relative;
	const std::filesystem::path directory_relative = "vegetation/library";

	const AshEngine::VegetationFileInspection absent =
		file_ops.InspectPath(root.Path(), file_relative);
	REQUIRE(absent.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(absent.canonical_relative_path == file_relative);
	CHECK_FALSE(absent.resolved_absolute_path.empty());
	CHECK_FALSE(absent.canonical_identity.empty());
	CHECK_FALSE(absent.file_identity.available);
	CHECK_FALSE(absent.exists);
	CHECK_FALSE(absent.is_regular_file);

	const std::vector<uint8_t> bytes = VegetationTest::MinimalLayerBytes();
	root.Write(file_relative, bytes);
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), directory_relative) ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(file_ops.EnsureDirectoryTree(root.Path(), directory_relative) ==
		AshEngine::VegetationFileResultStatus::Succeeded);

	const AshEngine::VegetationFileInspection file =
		file_ops.InspectPath(root.Path(), file_relative);
	CHECK(file.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(file.exists);
	CHECK(file.is_regular_file);
	CHECK(file.file_identity.available);
	const AshEngine::VegetationFileInspection directory =
		file_ops.InspectPath(root.Path(), directory_relative);
	CHECK(directory.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(directory.exists);
	CHECK_FALSE(directory.is_regular_file);
	CHECK(directory.file_identity.available);

	const AshEngine::VegetationFileInspection absolute =
		file_ops.InspectPath(root.Path(), file_absolute);
	CHECK(absolute.status == AshEngine::VegetationFileResultStatus::InvalidPath);
	CHECK(absolute.canonical_relative_path.empty());
	CHECK(absolute.resolved_absolute_path.empty());
	CHECK(absolute.canonical_identity.empty());
	CHECK_FALSE(absolute.file_identity.available);
	CHECK_FALSE(absolute.exists);
	CHECK_FALSE(absolute.is_regular_file);
	const AshEngine::VegetationFileInspection dot_segment =
		file_ops.InspectPath(root.Path(), "vegetation/../escape.AshVegetationLayer");
	CHECK(dot_segment.status == AshEngine::VegetationFileResultStatus::InvalidPath);
	CHECK(dot_segment.canonical_relative_path.empty());
	CHECK(dot_segment.resolved_absolute_path.empty());
	CHECK(dot_segment.canonical_identity.empty());

	const AshEngine::VegetationFileBytesResult missing =
		file_ops.ReadAllBytes(root.Path() / "missing.AshVegetationLayer", bytes.size());
	CHECK(missing.status == AshEngine::VegetationFileResultStatus::NotFound);
	CHECK(missing.bytes.empty());
	const AshEngine::VegetationFileBytesResult exact =
		file_ops.ReadAllBytes(file_absolute, bytes.size());
	CHECK(exact.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(exact.bytes == bytes);
	const AshEngine::VegetationFileBytesResult oversized =
		file_ops.ReadAllBytes(file_absolute, bytes.size() - 1);
	CHECK(oversized.status == AshEngine::VegetationFileResultStatus::LimitExceeded);
	CHECK(oversized.bytes.empty());
}

TEST_CASE("Vegetation storage sibling writer is contiguous bounded and closes once")
{
	VegetationTest::ScopedAssetRoot root("storage-writer-contract");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/meadow.AshVegetationLayer";

	AshEngine::VegetationStageFileResult stage =
		file_ops.CreateUniqueSiblingStageFile(target, 17);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE_FALSE(stage.owned_stage_file.empty());
	REQUIRE(stage.file_identity.available);
	REQUIRE(stage.writer != nullptr);
	CHECK(stage.owned_stage_file.parent_path() == target.parent_path());
	CHECK(std::filesystem::exists(stage.owned_stage_file));

	const std::array<uint8_t, 4> first{ 1, 2, 3, 4 };
	const std::array<uint8_t, 3> second{ 5, 6, 7 };
	const AshEngine::VegetationByteSpan empty{ first.data(), 0 };
	CHECK_FALSE(stage.writer->WriteBlock(0, empty));
	std::vector<uint8_t> too_large((1024u * 1024u) + 1u, 0x5a);
	CHECK_FALSE(stage.writer->WriteBlock(
		0, { too_large.data(), too_large.size() }));
	REQUIRE(stage.writer->WriteBlock(0, { first.data(), first.size() }));
	CHECK_FALSE(stage.writer->WriteBlock(3, { second.data(), second.size() }));
	REQUIRE(stage.writer->WriteBlock(4, { second.data(), second.size() }));
	REQUIRE(stage.writer->FlushAndClose());
	CHECK_FALSE(stage.writer->WriteBlock(7, { first.data(), first.size() }));
	CHECK_FALSE(stage.writer->FlushAndClose());

	const std::vector<uint8_t> expected{ 1, 2, 3, 4, 5, 6, 7 };
	CHECK(VegetationTest::ReadAllBytes(stage.owned_stage_file) == expected);
	CHECK(file_ops.RemoveOwnedStageFile(
		stage.owned_stage_file, stage.file_identity));
	CHECK_FALSE(std::filesystem::exists(stage.owned_stage_file));
}

TEST_CASE("Vegetation stage file registry abandons replaced identity without deleting replacement")
{
	VegetationTest::ScopedAssetRoot root("storage-stage-file-identity-cleanup");
	AshEngine::IVegetationFileOps& file_ops =
		AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/meadow.AshVegetationLayer";
	AshEngine::VegetationStageFileResult owned =
		file_ops.CreateUniqueSiblingStageFile(target, 18);
	AshEngine::VegetationStageFileResult replacement =
		file_ops.CreateUniqueSiblingStageFile(target, 1801);
	REQUIRE(owned.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(replacement.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(owned.file_identity.available);
	REQUIRE(replacement.file_identity.available);
	REQUIRE(owned.file_identity.volume_serial_number ==
		replacement.file_identity.volume_serial_number);
	REQUIRE(owned.file_identity.file_index != replacement.file_identity.file_index);
	const std::array<uint8_t, 4> owned_bytes{ 1, 2, 3, 4 };
	const std::array<uint8_t, 4> replacement_bytes{ 9, 8, 7, 6 };
	REQUIRE(owned.writer->WriteBlock(
		0, { owned_bytes.data(), owned_bytes.size() }));
	REQUIRE(replacement.writer->WriteBlock(
		0, { replacement_bytes.data(), replacement_bytes.size() }));
	REQUIRE(owned.writer->FlushAndClose());
	REQUIRE(replacement.writer->FlushAndClose());
	owned.writer.reset();
	replacement.writer.reset();

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		owned.owned_stage_file, owned.file_identity));
	REQUIRE(file_ops.RemoveOwnedStageFile(
		owned.owned_stage_file, owned.file_identity));
	std::error_code rename_error{};
	std::filesystem::rename(
		replacement.owned_stage_file, owned.owned_stage_file, rename_error);
	REQUIRE_FALSE(rename_error);

	CHECK(registry.CleanupStageFile(owned.owned_stage_file, file_ops));
	CHECK(registry.empty());
	const std::filesystem::path stage_relative =
		owned.owned_stage_file.lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection survivor =
		file_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(survivor.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(survivor.exists);
	CHECK(survivor.is_regular_file);
	CHECK(survivor.file_identity.volume_serial_number ==
		replacement.file_identity.volume_serial_number);
	CHECK(survivor.file_identity.file_index == replacement.file_identity.file_index);
	const AshEngine::VegetationFileBytesResult survivor_bytes =
		file_ops.ReadAllBytes(owned.owned_stage_file, replacement_bytes.size());
	REQUIRE(survivor_bytes.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(survivor_bytes.bytes == std::vector<uint8_t>(
		replacement_bytes.begin(), replacement_bytes.end()));
	CHECK(file_ops.RemoveOwnedStageFile(
		owned.owned_stage_file, replacement.file_identity));
}

TEST_CASE("Vegetation storage atomic replace never creates a missing target")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-only");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/missing.AshVegetationLayer";
	AshEngine::VegetationStageFileResult stage =
		file_ops.CreateUniqueSiblingStageFile(target, 19);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	const std::array<uint8_t, 3> bytes{ 1, 2, 3 };
	REQUIRE(stage.writer->WriteBlock(0, { bytes.data(), bytes.size() }));
	REQUIRE(stage.writer->FlushAndClose());
	stage.writer.reset();

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		stage.owned_stage_file, stage.file_identity));
	const AshEngine::VegetationAtomicReplaceResult replaced =
		file_ops.AtomicReplace(stage.owned_stage_file, target, registry);
	CHECK(replaced.status == AshEngine::VegetationAtomicReplaceStatus::TargetPreserved);
	CHECK(replaced.recovery_path.empty());
	CHECK_FALSE(std::filesystem::exists(target));
	CHECK(std::filesystem::exists(stage.owned_stage_file));
	CHECK(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	CHECK(registry.empty());
}

TEST_CASE("Vegetation storage failed replace policy distinguishes 1177 recovery artifacts")
{
	using Action = AshEngine::VegetationFailedReplaceAction;
	using State = AshEngine::VegetationReplacePathState;
	constexpr uint32_t unable_to_move_replacement_2 = 1177u;
	constexpr uint32_t unable_to_remove_replaced = 1175u;

	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_move_replacement_2, State::Missing, State::PresentRegular) ==
		Action::RestoreBackup);
	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_move_replacement_2, State::Missing, State::Missing) ==
		Action::RetainStage);
	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_move_replacement_2, State::Missing, State::ProbeFailed) ==
		Action::RetainBackup);
	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_move_replacement_2, State::Missing, State::Invalid) ==
		Action::RetainBackup);
	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_remove_replaced, State::PresentRegular, State::ProbeFailed) ==
		Action::TargetPreserved);
	CHECK(AshEngine::select_vegetation_failed_replace_action(
		unable_to_remove_replaced, State::ProbeFailed, State::ProbeFailed) ==
		Action::RetainStage);
}

static_assert(!std::is_aggregate_v<AshEngine::VegetationPreparedLayerWrite>,
	"Prepared Layer writes must not expose forgeable aggregate state");
static_assert(!std::is_copy_constructible_v<AshEngine::VegetationPreparedLayerWrite>,
	"Prepared Layer writes must not be copyable capabilities");
static_assert(std::is_move_constructible_v<AshEngine::VegetationPreparedLayerWrite>,
	"Prepared Layer writes must support ownership transfer from prepare");
static_assert(!std::is_copy_assignable_v<AshEngine::VegetationPreparedLayerWrite> &&
	!std::is_move_assignable_v<AshEngine::VegetationPreparedLayerWrite>,
	"Prepared Layer writes must not be rebound after construction");

TEST_CASE("Vegetation storage move transfers the only usable prepared capability")
{
	VegetationTest::ScopedAssetRoot root("storage-prepared-move");
	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	AshEngine::VegetationPreparedLayerWrite source =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), "vegetation/copy.AshVegetationLayer",
			VegetationTest::MinimalLayerSnapshot(), 20,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), registry);
	REQUIRE(source.status() == AshEngine::VegetationStorageStatus::Prepared);
	const std::filesystem::path stage_path = source.stage_path();
	AshEngine::VegetationPreparedLayerWrite transferred(std::move(source));

	CHECK(source.status() == AshEngine::VegetationStorageStatus::Failed);
	CHECK(source.stage_path().empty());
	CHECK(source.operation_serial() == 0);
	const AshEngine::VegetationStorageResult rejected =
		AshEngine::commit_vegetation_layer_write(
			source, 20, VegetationTest::ActiveControl(std::chrono::seconds(1)), registry);
	CHECK(rejected.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(std::filesystem::exists(stage_path));
	CHECK(registry.OwnsStageFile(stage_path));

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_copy_as(
			transferred, 20, VegetationTest::ActiveControl(std::chrono::seconds(1)), registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	CHECK(registry.empty());
}

TEST_CASE("Vegetation storage cleanup retries only retained owned stage files")
{
	VegetationTest::ScopedAssetRoot root("storage-targeted-cleanup");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/meadow.AshVegetationLayer";
	AshEngine::VegetationStageFileResult first =
		default_ops.CreateUniqueSiblingStageFile(target, 21);
	AshEngine::VegetationStageFileResult second =
		default_ops.CreateUniqueSiblingStageFile(target, 22);
	REQUIRE(first.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(second.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(first.owned_stage_file != second.owned_stage_file);
	REQUIRE(first.writer->WriteBlock(0, { reinterpret_cast<const uint8_t*>("a"), 1 }));
	REQUIRE(second.writer->WriteBlock(0, { reinterpret_cast<const uint8_t*>("b"), 1 }));
	REQUIRE(first.writer->FlushAndClose());
	REQUIRE(second.writer->FlushAndClose());

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		first.owned_stage_file, first.file_identity));
	REQUIRE(registry.TrackStageFile(
		second.owned_stage_file, second.file_identity));
	ScriptedVegetationFileOps scripted(default_ops);
	scripted.failing_stage_file = first.owned_stage_file;
	scripted.remaining_remove_failures = 2;

	CHECK_FALSE(registry.CleanupStageFile(first.owned_stage_file, scripted));
	CHECK(registry.OwnsStageFile(first.owned_stage_file));
	CHECK(registry.OwnsStageFile(second.owned_stage_file));
	CHECK(CountPath(scripted.removed_stage_files, first.owned_stage_file) == 1);
	CHECK(CountPath(scripted.removed_stage_files, second.owned_stage_file) == 0);
	CHECK(std::filesystem::exists(target.parent_path()));

	const AshEngine::VegetationOwnedStageCleanupStatus retained = registry.RetryAll(scripted);
	CHECK_FALSE(retained.all_removed);
	CHECK(retained.retained_stage_files ==
		std::vector<std::filesystem::path>{ first.owned_stage_file });
	CHECK(registry.OwnsStageFile(first.owned_stage_file));
	CHECK_FALSE(registry.OwnsStageFile(second.owned_stage_file));
	CHECK(std::filesystem::exists(first.owned_stage_file));
	CHECK_FALSE(std::filesystem::exists(second.owned_stage_file));
	CHECK(std::filesystem::exists(target.parent_path()));

	const AshEngine::VegetationOwnedStageCleanupStatus cleaned = registry.RetryAll(scripted);
	CHECK(cleaned.all_removed);
	CHECK(cleaned.retained_stage_files.empty());
	CHECK(registry.empty());
	CHECK_FALSE(std::filesystem::exists(first.owned_stage_file));
	CHECK(std::filesystem::exists(target.parent_path()));
}

TEST_CASE("Vegetation storage removes a long owned stage tree exactly")
{
	VegetationTest::ScopedAssetRoot root("storage-long-owned-tree-cleanup");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	constexpr size_t store_root_target_size = 248u;
	std::filesystem::path store_relative = "long-owned-tree-parent";
	while ((root.Path() / store_relative / "deterministic-segment")
		.lexically_normal().native().size() <= store_root_target_size)
	{
		store_relative /= "deterministic-segment";
	}
	const std::filesystem::path unpadded_store_root =
		(root.Path() / store_relative).lexically_normal();
	REQUIRE(unpadded_store_root.native().size() <= store_root_target_size);
	if (unpadded_store_root.native().size() < store_root_target_size)
	{
		const size_t one_character_size =
			(unpadded_store_root / "x").lexically_normal().native().size();
		REQUIRE(one_character_size <= store_root_target_size);
		store_relative /=
			std::string(store_root_target_size - one_character_size + 1u, 'x');
	}
	const std::filesystem::path store_root =
		(root.Path() / store_relative).lexically_normal();
	REQUIRE(store_root.native().size() == store_root_target_size);
	REQUIRE(store_root.native().size() < 260);
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), store_relative) ==
		AshEngine::VegetationFileResultStatus::Succeeded);

	AshEngine::VegetationStageTreeResult stage =
		file_ops.CreateUniqueStageTree(store_root, 21001);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(stage.owned_stage_root.native().size() > 260);
	const std::filesystem::path stage_relative =
		stage.owned_stage_root.lexically_relative(root.Path());
	REQUIRE_FALSE(stage_relative.empty());
	const std::filesystem::path child_relative =
		stage_relative / "nested-a" / "nested-b";
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), child_relative) ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationStageFileResult child = file_ops.CreateOwnedStageFile(
		stage.owned_stage_root,
		"nested-a/nested-b/.ashveg-layer-stage-direct.tmp");
	REQUIRE(child.status == AshEngine::VegetationFileResultStatus::Succeeded);
	const std::array<uint8_t, 4> bytes{ 0x41, 0x53, 0x56, 0x43 };
	REQUIRE(child.writer->WriteBlock(0, { bytes.data(), bytes.size() }));
	REQUIRE(child.writer->FlushAndClose());
	child.writer.reset();

	const AshEngine::VegetationFileInspection retained =
		file_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(retained.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(retained.exists);
	CHECK_FALSE(retained.is_regular_file);
	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageTree(stage.owned_stage_root, stage.file_identity));
	CHECK(registry.CleanupStageTree(stage.owned_stage_root, file_ops));
	CHECK(registry.empty());

	const AshEngine::VegetationFileInspection removed =
		file_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(removed.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK_FALSE(removed.exists);
	CHECK_FALSE(removed.is_regular_file);
}

#if defined(_WIN32)
TEST_CASE("Vegetation storage owned tree cleanup never follows a child directory link")
{
	VegetationTest::ScopedAssetRoot root("storage-owned-tree-child-link");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "outside") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "store") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::array<uint8_t, 8> sentinel_bytes{
		0x73, 0x65, 0x6e, 0x74, 0x69, 0x6e, 0x65, 0x6c };
	root.Write("outside/sentinel.bin",
		std::vector<uint8_t>(sentinel_bytes.begin(), sentinel_bytes.end()));
	AshEngine::VegetationStageTreeResult stage = file_ops.CreateUniqueStageTree(
		root.Path() / "store", 21002);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path link = stage.owned_stage_root / "outside-link";
	const std::filesystem::path outside = root.Path() / "outside";
	BOOL created = CreateSymbolicLinkW(
		ExtendedWindowsPath(link).c_str(), ExtendedWindowsPath(outside).c_str(),
		SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2u);
	if (created == FALSE && GetLastError() == ERROR_INVALID_PARAMETER)
	{
		created = CreateSymbolicLinkW(
			ExtendedWindowsPath(link).c_str(), ExtendedWindowsPath(outside).c_str(),
			SYMBOLIC_LINK_FLAG_DIRECTORY);
	}
	if (created == FALSE)
	{
		MESSAGE("Directory-link creation unavailable; Win32 error " << GetLastError());
		CHECK(file_ops.RemoveOwnedStageTree(
			stage.owned_stage_root, stage.file_identity));
		return;
	}

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageTree(stage.owned_stage_root, stage.file_identity));
	CHECK(registry.CleanupStageTree(stage.owned_stage_root, file_ops));
	CHECK(registry.empty());
	const AshEngine::VegetationFileInspection sentinel =
		file_ops.InspectPath(root.Path(), "outside/sentinel.bin");
	REQUIRE(sentinel.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(sentinel.exists);
	CHECK(sentinel.is_regular_file);
	const AshEngine::VegetationFileBytesResult bytes =
		file_ops.ReadAllBytes(sentinel.resolved_absolute_path, sentinel_bytes.size());
	REQUIRE(bytes.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(bytes.bytes ==
		std::vector<uint8_t>(sentinel_bytes.begin(), sentinel_bytes.end()));
}

TEST_CASE("Vegetation storage directory pin rejects namespace mutation until retry")
{
	VegetationTest::ScopedAssetRoot root("storage-owned-tree-pin");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "store") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationStageTreeResult stage = file_ops.CreateUniqueStageTree(
		root.Path() / "store", 21003);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageTree(stage.owned_stage_root, stage.file_identity));

	const HANDLE pin = CreateFileW(
		ExtendedWindowsPath(stage.owned_stage_root).c_str(),
		DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	REQUIRE(pin != INVALID_HANDLE_VALUE);
	const std::filesystem::path renamed =
		stage.owned_stage_root.parent_path() /
		(stage.owned_stage_root.filename().wstring() + L"-renamed");
	SetLastError(ERROR_SUCCESS);
	CHECK_FALSE(MoveFileExW(
		ExtendedWindowsPath(stage.owned_stage_root).c_str(),
		ExtendedWindowsPath(renamed).c_str(), MOVEFILE_WRITE_THROUGH));
	const DWORD rename_error = GetLastError();
	const bool rename_was_rejected =
		rename_error == ERROR_SHARING_VIOLATION || rename_error == ERROR_ACCESS_DENIED;
	CHECK(rename_was_rejected);
	SetLastError(ERROR_SUCCESS);
	CHECK_FALSE(RemoveDirectoryW(
		ExtendedWindowsPath(stage.owned_stage_root).c_str()));
	const DWORD remove_error = GetLastError();
	const bool remove_was_rejected =
		remove_error == ERROR_SHARING_VIOLATION || remove_error == ERROR_ACCESS_DENIED;
	CHECK(remove_was_rejected);
	SetLastError(ERROR_SUCCESS);
	const HANDLE blocked_writer = CreateFileW(
		ExtendedWindowsPath(stage.owned_stage_root).c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	CHECK(blocked_writer == INVALID_HANDLE_VALUE);
	CHECK(GetLastError() == ERROR_SHARING_VIOLATION);
	CHECK_FALSE(registry.CleanupStageTree(stage.owned_stage_root, file_ops));
	CHECK(registry.OwnsStageTree(stage.owned_stage_root));
	REQUIRE(CloseHandle(pin) != FALSE);
	const HANDLE allowed_writer = CreateFileW(
		ExtendedWindowsPath(stage.owned_stage_root).c_str(), GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	REQUIRE(allowed_writer != INVALID_HANDLE_VALUE);
	REQUIRE(CloseHandle(allowed_writer) != FALSE);

	CHECK(registry.CleanupStageTree(stage.owned_stage_root, file_ops));
	CHECK(registry.empty());
	const std::filesystem::path stage_relative =
		stage.owned_stage_root.lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection removed =
		file_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(removed.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK_FALSE(removed.exists);
}

TEST_CASE("Vegetation storage tree cleanup never takes over a same-name replacement")
{
	VegetationTest::ScopedAssetRoot root("storage-owned-tree-replacement");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "store") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationStageTreeResult stage = file_ops.CreateUniqueStageTree(
		root.Path() / "store", 21004);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(stage.file_identity.available);
	const std::filesystem::path exact = stage.owned_stage_root;
	const std::filesystem::path original_away = exact.parent_path() /
		(exact.filename().wstring() + L"-original");
	const std::filesystem::path replacement_away = exact.parent_path() /
		(exact.filename().wstring() + L"-replacement");
	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageTree(exact, stage.file_identity));
	REQUIRE(MoveFileExW(
		ExtendedWindowsPath(exact).c_str(),
		ExtendedWindowsPath(original_away).c_str(), MOVEFILE_WRITE_THROUGH));
	REQUIRE(CreateDirectoryW(ExtendedWindowsPath(exact).c_str(), nullptr));
	AshEngine::VegetationStageFileResult sentinel = file_ops.CreateOwnedStageFile(
		exact, ".ashveg-layer-stage-replacement-sentinel.tmp");
	REQUIRE(sentinel.status == AshEngine::VegetationFileResultStatus::Succeeded);
	const std::array<uint8_t, 8> sentinel_bytes{
		0x72, 0x65, 0x70, 0x6c, 0x61, 0x63, 0x65, 0x64 };
	REQUIRE(sentinel.writer->WriteBlock(
		0, { sentinel_bytes.data(), sentinel_bytes.size() }));
	REQUIRE(sentinel.writer->FlushAndClose());
	sentinel.writer.reset();
	const std::filesystem::path exact_relative = exact.lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection replacement =
		file_ops.InspectPath(root.Path(), exact_relative);
	REQUIRE(replacement.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(replacement.exists);
	REQUIRE_FALSE(replacement.is_regular_file);
	REQUIRE(replacement.file_identity.available);
	const bool replacement_is_distinct =
		replacement.file_identity.file_index != stage.file_identity.file_index ||
		replacement.file_identity.volume_serial_number !=
			stage.file_identity.volume_serial_number;
	CHECK(replacement_is_distinct);

	CHECK_FALSE(registry.CleanupStageTree(exact, file_ops));
	CHECK(registry.OwnsStageTree(exact));
	const AshEngine::VegetationOwnedStageCleanupStatus retained = registry.RetryAll(file_ops);
	CHECK_FALSE(retained.all_removed);
	CHECK(retained.retained_stage_trees ==
		std::vector<std::filesystem::path>{ exact });
	const AshEngine::VegetationFileBytesResult preserved = file_ops.ReadAllBytes(
		exact / ".ashveg-layer-stage-replacement-sentinel.tmp",
		sentinel_bytes.size());
	REQUIRE(preserved.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(preserved.bytes ==
		std::vector<uint8_t>(sentinel_bytes.begin(), sentinel_bytes.end()));

	REQUIRE(MoveFileExW(
		ExtendedWindowsPath(exact).c_str(),
		ExtendedWindowsPath(replacement_away).c_str(), MOVEFILE_WRITE_THROUGH));
	REQUIRE(MoveFileExW(
		ExtendedWindowsPath(original_away).c_str(),
		ExtendedWindowsPath(exact).c_str(), MOVEFILE_WRITE_THROUGH));
	const AshEngine::VegetationOwnedStageCleanupStatus cleaned = registry.RetryAll(file_ops);
	CHECK(cleaned.all_removed);
	CHECK(registry.empty());
	const AshEngine::VegetationFileInspection exact_missing =
		file_ops.InspectPath(root.Path(), exact_relative);
	REQUIRE(exact_missing.status == AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK_FALSE(exact_missing.exists);
	const std::filesystem::path replacement_relative =
		replacement_away.lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection replacement_retained =
		file_ops.InspectPath(root.Path(), replacement_relative);
	REQUIRE(replacement_retained.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	CHECK(replacement_retained.exists);
	CHECK(replacement_retained.file_identity.available);
	CHECK(replacement_retained.file_identity.volume_serial_number ==
		replacement.file_identity.volume_serial_number);
	CHECK(replacement_retained.file_identity.file_index ==
		replacement.file_identity.file_index);
	CHECK(file_ops.RemoveOwnedStageTree(
		replacement_away, replacement.file_identity));
}
#endif

TEST_CASE("Vegetation storage cleanup never removes a recovery-protected stage file")
{
	VegetationTest::ScopedAssetRoot root("storage-recovery-protection");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/meadow.AshVegetationLayer";
	AshEngine::VegetationStageFileResult stage =
		file_ops.CreateUniqueSiblingStageFile(target, 23);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(stage.writer->WriteBlock(
		0, { reinterpret_cast<const uint8_t*>("recovery"), 8 }));
	REQUIRE(stage.writer->FlushAndClose());
	stage.writer.reset();

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		stage.owned_stage_file, stage.file_identity));
	REQUIRE(registry.RetainStageFileForRecovery(stage.owned_stage_file));
	CHECK(registry.IsRecoveryStageFile(stage.owned_stage_file));
	CHECK_FALSE(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	const AshEngine::VegetationOwnedStageCleanupStatus retained = registry.RetryAll(file_ops);
	CHECK_FALSE(retained.all_removed);
	CHECK(retained.retained_recovery_stage_files ==
		std::vector<std::filesystem::path>{ stage.owned_stage_file });
	CHECK(std::filesystem::exists(stage.owned_stage_file));

	REQUIRE(registry.ReleaseRecoveryStageFile(stage.owned_stage_file));
	CHECK_FALSE(registry.IsRecoveryStageFile(stage.owned_stage_file));
	const AshEngine::VegetationOwnedStageCleanupStatus cleaned = registry.RetryAll(file_ops);
	CHECK(cleaned.all_removed);
	CHECK_FALSE(std::filesystem::exists(stage.owned_stage_file));
}

TEST_CASE("Vegetation storage consumed bookkeeping tolerates an exact cleanup in flight")
{
	VegetationTest::ScopedAssetRoot root("storage-forget-in-cleanup");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	AshEngine::VegetationStageFileResult stage = default_ops.CreateUniqueSiblingStageFile(
		root.Path() / "vegetation/meadow.AshVegetationLayer", 25);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(stage.writer->WriteBlock(
		0, { reinterpret_cast<const uint8_t*>("cleanup"), 7 }));
	REQUIRE(stage.writer->FlushAndClose());
	stage.writer.reset();

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		stage.owned_stage_file, stage.file_identity));
	ScriptedCommitFileOps scripted(default_ops);
	std::mutex mutex{};
	std::condition_variable condition{};
	bool entered = false;
	bool release = false;
	scripted.remove_stage = [&](const std::filesystem::path& path,
		const AshEngine::VegetationFileIdentity& expected_identity)
	{
		{
			std::unique_lock<std::mutex> lock(mutex);
			entered = true;
			condition.notify_one();
			condition.wait(lock, [&] { return release; });
		}
		return default_ops.RemoveOwnedStageFile(path, expected_identity);
	};
	bool cleanup_succeeded = false;
	std::thread cleanup([&]
	{
		cleanup_succeeded = registry.CleanupStageFile(stage.owned_stage_file, scripted);
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [&] { return entered; });
	}
	CHECK(registry.ForgetConsumedStageFile(stage.owned_stage_file));
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	condition.notify_one();
	cleanup.join();
	CHECK(cleanup_succeeded);
	CHECK(registry.empty());
}

TEST_CASE("Vegetation storage publish state pins an owned stage until an atomic resolution")
{
	VegetationTest::ScopedAssetRoot root("storage-publish-pin");
	AshEngine::IVegetationFileOps& file_ops = AshEngine::get_default_vegetation_file_ops();
	REQUIRE(file_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	const std::filesystem::path target =
		root.Path() / "vegetation/meadow.AshVegetationLayer";
	AshEngine::VegetationStageFileResult stage =
		file_ops.CreateUniqueSiblingStageFile(target, 24);
	REQUIRE(stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(stage.writer->WriteBlock(
		0, { reinterpret_cast<const uint8_t*>("publish"), 7 }));
	REQUIRE(stage.writer->FlushAndClose());
	stage.writer.reset();

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	REQUIRE(registry.TrackStageFile(
		stage.owned_stage_file, stage.file_identity));
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file, target));
	CHECK_FALSE(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	const AshEngine::VegetationOwnedStageCleanupStatus pinned = registry.RetryAll(file_ops);
	CHECK_FALSE(pinned.all_removed);
	REQUIRE(pinned.retained_stage_files.size() == 1);
	CHECK(pinned.retained_stage_files.front() == stage.owned_stage_file);
	REQUIRE(pinned.retained_recovery_stage_files.size() == 1);
	CHECK(pinned.retained_recovery_stage_files.front() == stage.owned_stage_file);
	CHECK(std::filesystem::exists(stage.owned_stage_file));
	// A publish pin is the fail-closed recovery state when a terminal registry
	// transition cannot be confirmed. The caller must be able to surface and
	// explicitly release that exact protected path after AtomicReplace returns.
	CHECK(registry.IsRecoveryStageFile(stage.owned_stage_file));
	REQUIRE(registry.ReleaseRecoveryStageFile(stage.owned_stage_file));
	CHECK_FALSE(registry.IsRecoveryStageFile(stage.owned_stage_file));
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file, target));

	REQUIRE(registry.ResolveStageFilePublish(
		stage.owned_stage_file,
		AshEngine::VegetationStageFilePublishResolution::RecoveryRequired));
	CHECK(registry.IsRecoveryStageFile(stage.owned_stage_file));
	CHECK_FALSE(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	REQUIRE(registry.ReleaseRecoveryStageFile(stage.owned_stage_file));
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file, target));
	REQUIRE(registry.ResolveStageFilePublish(
		stage.owned_stage_file,
		AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
	CHECK(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	CHECK(registry.empty());
}

TEST_CASE("Vegetation storage atomic recovery provenance is bound to source and target")
{
	VegetationTest::ScopedAssetRoot root("storage-atomic-recovery-provenance");
	const std::filesystem::path source_a =
		root.Path() / "vegetation/.ashveg-layer-stage-operation-a.tmp";
	const std::filesystem::path target_a =
		root.Path() / "vegetation/operation-a.AshVegetationLayer";
	const std::filesystem::path backup_a =
		root.Path() / "vegetation/.ashveg-layer-stage-replace-backup-operation-a.tmp";
	const std::filesystem::path generic_recovery =
		root.Path() / "vegetation/.ashveg-layer-stage-generic-recovery.tmp";
	const std::filesystem::path source_b =
		root.Path() / "vegetation/.ashveg-layer-stage-operation-b.tmp";
	const std::filesystem::path target_b =
		root.Path() / "vegetation/operation-b.AshVegetationLayer";
	const std::filesystem::path backup_b =
		root.Path() / "vegetation/.ashveg-layer-stage-replace-backup-operation-b.tmp";
	const std::filesystem::path source_a_case_variant =
		root.Path() / "VEGETATION/.ASHVEG-LAYER-STAGE-OPERATION-A.TMP";
	const std::filesystem::path target_a_case_variant =
		root.Path() / "VEGETATION/OPERATION-A.ASHVEGETATIONLAYER";

	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	const AshEngine::VegetationOwnedStageCleanupRegistry& const_registry = registry;
	const AshEngine::VegetationFileIdentity source_a_identity{ true, 1u, 1u };
	const AshEngine::VegetationFileIdentity source_b_identity{ true, 1u, 2u };
	const AshEngine::VegetationFileIdentity backup_a_identity{ true, 1u, 3u };
	const AshEngine::VegetationFileIdentity backup_b_identity{ true, 1u, 4u };
	static_assert(noexcept(registry.BeginStageFilePublish(source_a, target_a)));
	static_assert(noexcept(const_registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_a, backup_a_identity)));
	REQUIRE(registry.TrackStageFile(source_a, source_a_identity));
	REQUIRE(registry.BeginStageFilePublish(source_a, target_a));
	CHECK(registry.IsAtomicReplaceRecoveryStageFile(
		source_a, source_a, target_a, source_a_identity));
	CHECK_FALSE(registry.RetainStageFileForAtomicReplaceRecovery(
		generic_recovery, source_a, target_a, backup_a_identity));
	REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
		backup_a, source_a, target_a, backup_a_identity));
	CHECK(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_a, backup_a_identity));
	CHECK(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a_case_variant, target_a_case_variant, backup_a_identity));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_a, backup_b_identity));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_a, {}));

	REQUIRE(registry.TrackStageFile(source_b, source_b_identity));
	REQUIRE(registry.BeginStageFilePublish(source_b, target_b));
	REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
		backup_b, source_b, target_b, backup_b_identity));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_b, target_b, backup_a_identity));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_b, target_a, backup_a_identity));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_b, backup_a_identity));

	REQUIRE(registry.ReleaseRecoveryStageFile(backup_a));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		backup_a, source_a, target_a, backup_a_identity));
	REQUIRE(registry.ReleaseRecoveryStageFile(source_a));
	CHECK_FALSE(registry.IsAtomicReplaceRecoveryStageFile(
		source_a, source_a, target_a, source_a_identity));
}

TEST_CASE("Vegetation storage checked save never replaces an externally changed Layer")
{
	VegetationTest::ScopedAssetRoot root("storage-source-changed");
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(opened.snapshot != nullptr);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, *opened.snapshot, 7,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	REQUIRE_FALSE(prepared.stage_path().empty());
	const std::vector<uint8_t> external = VegetationTest::DifferentValidLayerBytes();
	VegetationTest::WriteAllBytes(target_absolute, external);

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 7, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::SourceChanged);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == external);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage classifies a grown source as externally changed")
{
	VegetationTest::ScopedAssetRoot root("storage-source-grew");
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(opened.snapshot != nullptr);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, *opened.snapshot, 8,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);

	std::vector<uint8_t> grown = VegetationTest::DifferentValidLayerBytes();
	grown.push_back(0x7f);
	VegetationTest::WriteAllBytes(target_absolute, grown);
	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 8, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::SourceChanged);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == grown);
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage rejects incomplete operation control before staging")
{
	VegetationTest::ScopedAssetRoot root("storage-invalid-control");
	AshEngine::VegetationOperationControl control{};
	control.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), "vegetation/new.AshVegetationLayer", std::nullopt,
			VegetationTest::MinimalLayerSnapshot(), 12, control, cleanup_registry);
	CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
	CHECK(prepared.stage_path().empty());
	CHECK(cleanup_registry.empty());
	CHECK_FALSE(std::filesystem::exists(root.Path() / "vegetation"));
}

TEST_CASE("Vegetation storage classifies cancellation and timeout before staging")
{
	VegetationTest::ScopedAssetRoot root("storage-staging-control");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	ScriptedCommitFileOps scripted(default_ops);
	AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(1));
	AshEngine::VegetationStorageStatus expected_status =
		AshEngine::VegetationStorageStatus::Cancelled;

	SUBCASE("cancelled")
	{
		const std::shared_ptr<std::atomic_bool> cancel_requested =
			std::const_pointer_cast<std::atomic_bool>(control.cancel_requested);
		cancel_requested->store(true, std::memory_order_release);
	}
	SUBCASE("timed out")
	{
		expected_status = AshEngine::VegetationStorageStatus::TimedOut;
		control.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
	}

	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), "vegetation/new.AshVegetationLayer",
			VegetationTest::MinimalLayerSnapshot(), 13, control, cleanup_registry, scripted);
	CHECK(prepared.status() == expected_status);
	CHECK(prepared.stage_path().empty());
	CHECK(scripted.inspect_call_count == 0);
	CHECK(cleanup_registry.empty());
	CHECK_FALSE(std::filesystem::exists(root.Path() / "vegetation"));
}

TEST_CASE("Vegetation storage preparation failures never publish and retain only failed cleanup")
{
	VegetationTest::ScopedAssetRoot root("storage-prepare-failures");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	ScriptedCommitFileOps scripted(default_ops);
	AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(1));
	const std::shared_ptr<ScriptedStageWriterState> writer_state =
		std::make_shared<ScriptedStageWriterState>();
	bool expect_retained_cleanup = false;

	SUBCASE("directory creation failure stops before staging")
	{
		scripted.ensure_directory = [](const std::filesystem::path&,
			const std::filesystem::path&)
		{
			return AshEngine::VegetationFileResultStatus::Failed;
		};
	}
	SUBCASE("stage creation failure stops before writing")
	{
		scripted.create_stage = [](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Failed;
			result.error = "injected stage create failure";
			return result;
		};
	}
	SUBCASE("write failure cleans the exact stage")
	{
		writer_state->fail_write = true;
	}
	SUBCASE("flush failure cleans the exact stage")
	{
		writer_state->fail_flush = true;
	}
	SUBCASE("cancellation observed after a completed write cleans the exact stage")
	{
		const std::shared_ptr<std::atomic_bool> cancel_requested =
			std::const_pointer_cast<std::atomic_bool>(control.cancel_requested);
		writer_state->after_write = [cancel_requested]()
		{
			cancel_requested->store(true, std::memory_order_release);
		};
	}
	SUBCASE("failed targeted cleanup remains registered for retry")
	{
		writer_state->fail_write = true;
		expect_retained_cleanup = true;
		scripted.remove_stage = [](
			const std::filesystem::path&,
			const AshEngine::VegetationFileIdentity&)
		{
			return false;
		};
	}

	if (!scripted.ensure_directory && !scripted.create_stage)
	{
		scripted.create_stage = [&](const std::filesystem::path& target, const uint64_t serial)
		{
			AshEngine::VegetationStageFileResult result =
				default_ops.CreateUniqueSiblingStageFile(target, serial);
			if (result.status == AshEngine::VegetationFileResultStatus::Succeeded)
			{
				result.writer = std::make_unique<ScriptedStageWriter>(
					std::move(result.writer), writer_state);
			}
			return result;
		};
	}

	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), "vegetation/new.AshVegetationLayer",
			VegetationTest::MinimalLayerSnapshot(), 15, control,
			cleanup_registry, scripted);
	CHECK(prepared.status() != AshEngine::VegetationStorageStatus::Prepared);
	CHECK_FALSE(std::filesystem::exists(
		root.Path() / "vegetation/new.AshVegetationLayer"));

	if (expect_retained_cleanup)
	{
		REQUIRE_FALSE(prepared.stage_path().empty());
		CHECK(cleanup_registry.OwnsStageFile(prepared.stage_path()));
		scripted.remove_stage = {};
		const AshEngine::VegetationOwnedStageCleanupStatus retried =
			cleanup_registry.RetryAll(scripted);
		CHECK(retried.all_removed);
		CHECK(cleanup_registry.empty());
	}
	else
	{
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation storage first save preserves a racing creator")
{
	VegetationTest::ScopedAssetRoot root("storage-first-save-race");
	const std::filesystem::path target_relative =
		"vegetation/nested/first.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, std::nullopt,
			VegetationTest::MinimalLayerSnapshot(), 9,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	CHECK(std::filesystem::exists(target_absolute.parent_path()));
	const std::vector<uint8_t> external = VegetationTest::DifferentValidLayerBytes();
	VegetationTest::WriteAllBytes(target_absolute, external);

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 9, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::AlreadyExists);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == external);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage Save Copy As is create-new and never rebinds the source")
{
	VegetationTest::ScopedAssetRoot root("storage-copy-race");
	const std::filesystem::path source_relative =
		"vegetation/source.AshVegetationLayer";
	const std::filesystem::path source_absolute = root.Path() / source_relative;
	const std::filesystem::path destination_relative =
		"vegetation/copy.AshVegetationLayer";
	const std::filesystem::path destination_absolute = root.Path() / destination_relative;
	const std::vector<uint8_t> source = VegetationTest::MinimalLayerBytes();
	root.Write(source_relative, source);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), destination_relative, VegetationTest::MinimalLayerSnapshot(), 11,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	const std::vector<uint8_t> external = VegetationTest::DifferentValidLayerBytes();
	VegetationTest::WriteAllBytes(destination_absolute, external);

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_copy_as(
			prepared, 11, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::AlreadyExists);
	CHECK(VegetationTest::ReadAllBytes(destination_absolute) == external);
	CHECK(VegetationTest::ReadAllBytes(source_absolute) == source);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage wrong commit API cleans only its bound prepared stage")
{
	VegetationTest::ScopedAssetRoot root("storage-wrong-commit-api");
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), "vegetation/copy.AshVegetationLayer",
			VegetationTest::MinimalLayerSnapshot(), 29,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	REQUIRE(std::filesystem::exists(prepared.stage_path()));

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 29, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK_FALSE(std::filesystem::exists(
		root.Path() / "vegetation/copy.AshVegetationLayer"));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage wrong cleanup registry cannot consume a prepared stage")
{
	VegetationTest::ScopedAssetRoot root("storage-wrong-cleanup-registry");
	AshEngine::VegetationOwnedStageCleanupRegistry owner_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), "vegetation/copy.AshVegetationLayer",
			VegetationTest::MinimalLayerSnapshot(), 30,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), owner_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	AshEngine::VegetationOwnedStageCleanupRegistry wrong_registry{};
	const AshEngine::VegetationFileInspection prepared_stage =
		AshEngine::get_default_vegetation_file_ops().InspectPath(
			root.Path(), prepared.stage_path().lexically_relative(root.Path()));
	REQUIRE(prepared_stage.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(prepared_stage.file_identity.available);
	REQUIRE(wrong_registry.TrackStageFile(
		prepared.stage_path(), prepared_stage.file_identity));

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_copy_as(
			prepared, 30, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			wrong_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(std::filesystem::exists(prepared.stage_path()));
	CHECK(owner_registry.OwnsStageFile(prepared.stage_path()));
	CHECK(wrong_registry.OwnsStageFile(prepared.stage_path()));
	REQUIRE(wrong_registry.ForgetConsumedStageFile(prepared.stage_path()));
	CHECK(owner_registry.CleanupStageFile(
		prepared.stage_path(), AshEngine::get_default_vegetation_file_ops()));
	CHECK(owner_registry.empty());
	CHECK(wrong_registry.empty());
}

TEST_CASE("Vegetation storage rejects illegal byte and stage result shapes")
{
	VegetationTest::ScopedAssetRoot root("storage-illegal-fileops-shapes");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	ScriptedCommitFileOps scripted(default_ops);

	SUBCASE("failed bounded read cannot carry bytes")
	{
		const std::filesystem::path target_relative =
			"vegetation/meadow.AshVegetationLayer";
		root.Write(target_relative, VegetationTest::MinimalLayerBytes());
		scripted.read = [](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationFileBytesResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Failed;
			result.bytes = VegetationTest::MinimalLayerBytes();
			result.error = "injected illegal bytes";
			return result;
		};

		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget(), scripted);
		CHECK(opened.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(opened.snapshot == nullptr);
		CHECK(opened.canonical_identity.empty());
		CHECK(scripted.read_call_count == 1);
	}

	SUBCASE("absent inspection cannot carry unavailable nonzero identity fields")
	{
		const std::filesystem::path target_relative =
			"vegetation/nonzero-absent-identity.AshVegetationLayer";
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			AshEngine::VegetationFileInspection result =
				default_ops.InspectPath(asset_root, path);
			REQUIRE(result.status == AshEngine::VegetationFileResultStatus::Succeeded);
			REQUIRE_FALSE(result.exists);
			result.file_identity.volume_serial_number = 17;
			result.file_identity.file_index = 23;
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 310,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(prepared.error().find("illegal result shape") != std::string::npos);
		if (!prepared.stage_path().empty() &&
			cleanup_registry.OwnsStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		}
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("successful stage must carry both an owned path and writer")
	{
		scripted.create_stage = [](const std::filesystem::path& target, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = target.parent_path() / "illegal-stage.tmp";
			result.file_identity = TestFileIdentity(1);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 31,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(prepared.stage_path().empty());
		CHECK(cleanup_registry.empty());
		CHECK_FALSE(std::filesystem::exists(
			root.Path() / "vegetation/new.AshVegetationLayer"));
	}

	SUBCASE("failed stage must not carry an owned path or writer")
	{
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path& target, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Failed;
			result.owned_stage_file =
				target.parent_path() / ".ashveg-layer-stage-illegal-payload.tmp";
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 32,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed stage cannot carry unavailable nonzero identity fields")
	{
		scripted.create_stage = [](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Failed;
			result.file_identity.volume_serial_number = 29;
			result.file_identity.file_index = 31;
			result.error = "injected ordinary stage failure";
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 311,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(prepared.error().find("illegal result shape") != std::string::npos);
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("successful stage cannot omit its stable file identity")
	{
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path& target, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file =
				target.parent_path() / ".ashveg-layer-stage-missing-identity.tmp";
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 320,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("create-new stage alias formed after the absent inspection is rejected before writing")
	{
		const std::filesystem::path target_relative =
			"vegetation/NewAlias.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::filesystem::path stage_alias =
			target_absolute.parent_path() / "NEWALIAS.ASHVEGETATIONLAYER";
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			std::ofstream created(stage_alias, std::ios::binary | std::ios::trunc);
			created.close();
			const AshEngine::VegetationFileInspection alias_inspection =
				default_ops.InspectPath(root.Path(), target_relative);
			REQUIRE(alias_inspection.status ==
				AshEngine::VegetationFileResultStatus::Succeeded);
			REQUIRE(alias_inspection.file_identity.available);

			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = stage_alias;
			result.file_identity = alias_inspection.file_identity;
			result.writer = std::make_unique<AliasWritingStageWriter>(
				stage_alias, writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 321,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		if (cleanup_registry.OwnsStageFile(stage_alias))
		{
			CHECK(cleanup_registry.ForgetConsumedStageFile(stage_alias));
		}
		CHECK(cleanup_registry.empty());
		std::error_code remove_error{};
		(void)std::filesystem::remove(stage_alias, remove_error);
		CHECK_FALSE(remove_error);
	}

	SUBCASE("cooperative target creation during stage creation preserves the target and cleans the distinct stage")
	{
		const std::filesystem::path target_relative =
			"vegetation/raced-create.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> rival_bytes = VegetationTest::MinimalLayerBytes();
		scripted.create_stage = [&](const std::filesystem::path& target, const uint64_t serial)
		{
			AshEngine::VegetationStageFileResult result =
				default_ops.CreateUniqueSiblingStageFile(target, serial);
			REQUIRE(result.status == AshEngine::VegetationFileResultStatus::Succeeded);
			root.Write(target_relative, rival_bytes);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 322,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::AlreadyExists);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == rival_bytes);
		CHECK_FALSE(scripted.last_created_stage.empty());
		CHECK_FALSE(std::filesystem::exists(scripted.last_created_stage));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed target reinspection retains the unverified stage as explicit recovery")
	{
		size_t inspection_count = 0;
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			++inspection_count;
			if (inspection_count != 2)
			{
				return default_ops.InspectPath(asset_root, path);
			}
			AshEngine::VegetationFileInspection failed{};
			failed.status = AshEngine::VegetationFileResultStatus::Failed;
			failed.error = "injected target reinspection failure";
			return failed;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/reinspect-failure.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 323,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(inspection_count == 3);
		CHECK_FALSE(scripted.last_created_stage.empty());
		CHECK(prepared.stage_path() == scripted.last_created_stage);
		CHECK(std::filesystem::exists(scripted.last_created_stage));
		CHECK(cleanup_registry.OwnsStageFile(scripted.last_created_stage));
		CHECK(cleanup_registry.IsRecoveryStageFile(scripted.last_created_stage));
		const AshEngine::VegetationOwnedStageCleanupStatus retained =
			cleanup_registry.RetryAll(default_ops);
		CHECK_FALSE(retained.all_removed);
		CHECK(retained.retained_recovery_stage_files ==
			std::vector<std::filesystem::path>{ scripted.last_created_stage });
		if (cleanup_registry.IsRecoveryStageFile(scripted.last_created_stage))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(scripted.last_created_stage));
			CHECK(cleanup_registry.CleanupStageFile(scripted.last_created_stage, default_ops));
		}
		else if (std::filesystem::exists(scripted.last_created_stage))
		{
			CHECK(default_ops.RemoveOwnedStageFile(
				scripted.last_created_stage,
				scripted.last_created_stage_identity));
		}
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed target reinspection cannot take another operation's registered stage")
	{
		const std::filesystem::path target_relative =
			"vegetation/reinspect-duplicate-owner.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		AshEngine::VegetationStageFileResult existing =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 324);
		REQUIRE(existing.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(existing.file_identity.available);
		existing.writer.reset();
		const std::filesystem::path existing_stage = existing.owned_stage_file;

		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		REQUIRE(cleanup_registry.TrackStageFile(
			existing_stage, existing.file_identity));
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = existing_stage;
			result.file_identity = existing.file_identity;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		size_t inspection_count = 0;
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			++inspection_count;
			if (inspection_count == 2)
			{
				AshEngine::VegetationFileInspection failed{};
				failed.status = AshEngine::VegetationFileResultStatus::Failed;
				failed.error = "injected target reinspection failure";
				return failed;
			}
			return default_ops.InspectPath(asset_root, path);
		};

		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 324,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(inspection_count == 3);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.OwnsStageFile(existing_stage));
		CHECK_FALSE(cleanup_registry.IsRecoveryStageFile(existing_stage));
		CHECK(std::filesystem::exists(existing_stage));
		CHECK(cleanup_registry.CleanupStageFile(existing_stage, default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed target reinspection cannot take a case-only alias of a registered stage")
	{
		const std::filesystem::path target_relative =
			"vegetation/reinspect-case-owner.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		AshEngine::VegetationStageFileResult existing =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 325);
		REQUIRE(existing.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(existing.file_identity.available);
		existing.writer.reset();
		const std::filesystem::path existing_stage = existing.owned_stage_file;
		std::wstring upper_name = existing_stage.filename().native();
		std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
			[](const wchar_t value)
			{
				return value >= L'a' && value <= L'z'
					? static_cast<wchar_t>(value - L'a' + L'A') : value;
			});
		const std::filesystem::path case_alias_stage =
			existing_stage.parent_path() / upper_name;
		REQUIRE(case_alias_stage != existing_stage);

		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		REQUIRE(cleanup_registry.TrackStageFile(
			existing_stage, existing.file_identity));
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = case_alias_stage;
			result.file_identity = existing.file_identity;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		size_t inspection_count = 0;
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			++inspection_count;
			if (inspection_count == 2)
			{
				AshEngine::VegetationFileInspection failed{};
				failed.status = AshEngine::VegetationFileResultStatus::Failed;
				failed.error = "injected target reinspection failure";
				return failed;
			}
			return default_ops.InspectPath(asset_root, path);
		};

		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 326,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(inspection_count == 3);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.OwnsStageFile(existing_stage));
		CHECK_FALSE(cleanup_registry.IsRecoveryStageFile(case_alias_stage));
		CHECK(std::filesystem::exists(existing_stage));
		CHECK(cleanup_registry.CleanupStageFile(existing_stage, default_ops));
		CHECK(cleanup_registry.empty());
		if (cleanup_registry.IsRecoveryStageFile(case_alias_stage))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(case_alias_stage));
			CHECK(cleanup_registry.ForgetConsumedStageFile(case_alias_stage));
		}
	}

	SUBCASE("failed target reinspection does not register a missing stage ghost")
	{
		const std::filesystem::path target_relative =
			"vegetation/reinspect-missing-stage.AshVegetationLayer";
		const std::filesystem::path missing_stage =
			root.Path() / "vegetation/.ashveg-missing-stage.tmp";
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = missing_stage;
			result.file_identity.available = true;
			result.file_identity.volume_serial_number = 1;
			result.file_identity.file_index = 2;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		size_t inspection_count = 0;
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			++inspection_count;
			if (inspection_count == 2)
			{
				AshEngine::VegetationFileInspection failed{};
				failed.status = AshEngine::VegetationFileResultStatus::Failed;
				failed.error = "injected target reinspection failure";
				return failed;
			}
			return default_ops.InspectPath(asset_root, path);
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 325,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(inspection_count == 3);
		CHECK_FALSE(*writer_used);
		CHECK_FALSE(std::filesystem::exists(missing_stage));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed target reinspection rejects stage identity drift without cleanup ownership")
	{
		const std::filesystem::path target_relative =
			"vegetation/reinspect-stage-drift.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		AshEngine::VegetationStageFileResult actual =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 327);
		AshEngine::VegetationStageFileResult reported =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 328);
		REQUIRE(actual.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(reported.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(actual.file_identity.available);
		REQUIRE(reported.file_identity.available);
		REQUIRE((actual.file_identity.volume_serial_number !=
			reported.file_identity.volume_serial_number ||
			actual.file_identity.file_index != reported.file_identity.file_index));
		actual.writer.reset();
		reported.writer.reset();
		const std::filesystem::path actual_stage = actual.owned_stage_file;
		const std::filesystem::path reported_stage = reported.owned_stage_file;
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = actual_stage;
			result.file_identity = reported.file_identity;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		size_t inspection_count = 0;
		scripted.inspect = [&](const std::filesystem::path& asset_root,
			const std::filesystem::path& path)
		{
			++inspection_count;
			if (inspection_count == 2)
			{
				AshEngine::VegetationFileInspection failed{};
				failed.status = AshEngine::VegetationFileResultStatus::Failed;
				failed.error = "injected target reinspection failure";
				return failed;
			}
			return default_ops.InspectPath(asset_root, path);
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), 329,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK(inspection_count == 3);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.empty());
		CHECK(std::filesystem::exists(actual_stage));
		CHECK(std::filesystem::exists(reported_stage));
		CHECK(default_ops.RemoveOwnedStageFile(
			actual_stage, actual.file_identity));
		CHECK(default_ops.RemoveOwnedStageFile(
			reported_stage, reported.file_identity));
	}

	SUBCASE("stage aliasing the target is rejected before the writer can mutate it")
	{
		const std::filesystem::path target_relative =
			"vegetation/existing.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		const AshEngine::VegetationFileInspection target_inspection =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_inspection.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_inspection.file_identity.available);
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [=](const std::filesystem::path& target, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = target;
			result.file_identity = target_inspection.file_identity;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision,
				VegetationTest::MinimalLayerSnapshot(), 33,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("case-only stage alias is rejected by file identity before writing")
	{
		const std::filesystem::path target_relative =
			"vegetation/CaseAlias.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::filesystem::path stage_alias =
			target_absolute.parent_path() / "CASEALIAS.ASHVEGETATIONLAYER";
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		const AshEngine::VegetationFileInspection target_inspection =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_inspection.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_inspection.file_identity.available);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [=](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = stage_alias;
			result.file_identity = target_inspection.file_identity;
			result.writer = std::make_unique<AliasWritingStageWriter>(stage_alias, writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 330,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		if (cleanup_registry.OwnsStageFile(stage_alias))
		{
			CHECK(cleanup_registry.ForgetConsumedStageFile(stage_alias));
		}
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("hard-link stage alias is rejected by file identity before writing")
	{
		const std::filesystem::path target_relative =
			"vegetation/hard-link-source.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::filesystem::path stage_alias =
			target_absolute.parent_path() / ".ashveg-layer-stage-hard-link.tmp";
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		std::error_code link_error{};
		std::filesystem::create_hard_link(target_absolute, stage_alias, link_error);
		REQUIRE_FALSE(link_error);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		const AshEngine::VegetationFileInspection target_inspection =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_inspection.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_inspection.file_identity.available);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [=](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = stage_alias;
			result.file_identity = target_inspection.file_identity;
			result.writer = std::make_unique<AliasWritingStageWriter>(stage_alias, writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 331,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		if (cleanup_registry.OwnsStageFile(stage_alias))
		{
			CHECK(cleanup_registry.ForgetConsumedStageFile(stage_alias));
		}
		CHECK(cleanup_registry.empty());
		std::error_code remove_error{};
		CHECK(std::filesystem::remove(stage_alias, remove_error));
		CHECK_FALSE(remove_error);
	}

	SUBCASE("stage outside the target directory is rejected before writing")
	{
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file =
				(root.Path() / ".ashveg-layer-stage-foreign.tmp").lexically_normal();
			result.file_identity = TestFileIdentity(2);
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 34,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("duplicate stage ownership rejection never deletes the existing owner file")
	{
		REQUIRE(default_ops.EnsureDirectoryTree(root.Path(), "vegetation") ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		const std::filesystem::path target =
			root.Path() / "vegetation/new.AshVegetationLayer";
		AshEngine::VegetationStageFileResult existing =
			default_ops.CreateUniqueSiblingStageFile(target, 35);
		REQUIRE(existing.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(existing.writer->WriteBlock(
			0, { reinterpret_cast<const uint8_t*>("owned"), 5 }));
		REQUIRE(existing.writer->FlushAndClose());
		existing.writer.reset();
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		REQUIRE(cleanup_registry.TrackStageFile(
			existing.owned_stage_file, existing.file_identity));
		const std::shared_ptr<bool> writer_used = std::make_shared<bool>(false);
		scripted.create_stage = [&](const std::filesystem::path&, const uint64_t)
		{
			AshEngine::VegetationStageFileResult result{};
			result.status = AshEngine::VegetationFileResultStatus::Succeeded;
			result.owned_stage_file = existing.owned_stage_file;
			result.file_identity = existing.file_identity;
			result.writer = std::make_unique<RecordingStageWriter>(writer_used);
			return result;
		};

		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 35,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(*writer_used);
		CHECK(std::filesystem::exists(existing.owned_stage_file));
		CHECK(cleanup_registry.OwnsStageFile(existing.owned_stage_file));
		CHECK(cleanup_registry.CleanupStageFile(existing.owned_stage_file, default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("illegal stage readback is cleaned without publishing")
	{
		scripted.read = [&](const std::filesystem::path& path, const uint64_t max_bytes)
		{
			if (!scripted.last_created_stage.empty() && path == scripted.last_created_stage)
			{
				AshEngine::VegetationFileBytesResult result{};
				result.status = AshEngine::VegetationFileResultStatus::Failed;
				result.bytes = VegetationTest::MinimalLayerBytes();
				result.error = "injected illegal stage readback";
				return result;
			}
			return default_ops.ReadAllBytes(path, max_bytes);
		};
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), "vegetation/new.AshVegetationLayer",
				VegetationTest::MinimalLayerSnapshot(), 32,
				VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);

		CHECK(prepared.status() == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(scripted.last_created_stage.empty());
		CHECK_FALSE(std::filesystem::exists(scripted.last_created_stage));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation storage rejects illegal lease result shapes without mutation")
{
	VegetationTest::ScopedAssetRoot root("storage-illegal-lease-shapes");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	replacement.layer_seed ^= 0x1234ull;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 41,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);

	SUBCASE("Acquired requires a non-null lease")
	{
		scripted.acquire = [](std::string_view,
			const AshEngine::VegetationOperationControl&)
		{
			AshEngine::VegetationFileLeaseResult result{};
			result.status = AshEngine::VegetationFileLeaseStatus::Acquired;
			return result;
		};
	}
	SUBCASE("non-acquired status forbids a lease")
	{
		scripted.acquire = [](std::string_view,
			const AshEngine::VegetationOperationControl&)
		{
			AshEngine::VegetationFileLeaseResult result{};
			result.status = AshEngine::VegetationFileLeaseStatus::Failed;
			result.lease = std::make_unique<TestVegetationFileLease>();
			return result;
		};
	}

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 41, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	CHECK(scripted.acquire_call_count == 1);
	CHECK(scripted.replace_call_count == 0);
	CHECK(scripted.create_new_call_count == 0);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage serial mismatch fails before lease and preserves target")
{
	VegetationTest::ScopedAssetRoot root("storage-serial-mismatch");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision,
			VegetationTest::MinimalLayerSnapshot(), 51,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 52, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::SourceChanged);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	CHECK(scripted.acquire_call_count == 0);
	CHECK(scripted.replace_call_count == 0);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage observes cancellation and timeout at commit checkpoints")
{
	VegetationTest::ScopedAssetRoot root("storage-control-checkpoints");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 61,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(1));
	const std::shared_ptr<std::atomic_bool> cancel_requested =
		std::const_pointer_cast<std::atomic_bool>(control.cancel_requested);
	AshEngine::VegetationStorageStatus expected_status =
		AshEngine::VegetationStorageStatus::Cancelled;

	SUBCASE("cancellation immediately after lease prevents target reread")
	{
		scripted.acquire = [&](const std::string_view identity,
			const AshEngine::VegetationOperationControl& acquire_control)
		{
			AshEngine::VegetationFileLeaseResult result =
				default_ops.AcquireNamedLease(identity, acquire_control);
			cancel_requested->store(true, std::memory_order_release);
			return result;
		};
	}
	SUBCASE("cancellation after source reread prevents publish")
	{
		scripted.read = [&](const std::filesystem::path& path, const uint64_t max_bytes)
		{
			AshEngine::VegetationFileBytesResult result =
				default_ops.ReadAllBytes(path, max_bytes);
			if (path == target_absolute)
			{
				cancel_requested->store(true, std::memory_order_release);
			}
			return result;
		};
	}
	SUBCASE("cancellation after staged-byte validation prevents publish")
	{
		scripted.read = [&](const std::filesystem::path& path, const uint64_t max_bytes)
		{
			AshEngine::VegetationFileBytesResult result =
				default_ops.ReadAllBytes(path, max_bytes);
			if (path == prepared.stage_path())
			{
				cancel_requested->store(true, std::memory_order_release);
			}
			return result;
		};
	}
	SUBCASE("lease timeout is preserved as a named outcome")
	{
		expected_status = AshEngine::VegetationStorageStatus::TimedOut;
		scripted.acquire = [](std::string_view,
			const AshEngine::VegetationOperationControl&)
		{
			AshEngine::VegetationFileLeaseResult result{};
			result.status = AshEngine::VegetationFileLeaseStatus::TimedOut;
			result.error = "injected lease timeout";
			return result;
		};
	}

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 61, control, cleanup_registry, scripted);
	CHECK(committed.status == expected_status);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	CHECK(scripted.acquire_call_count == 1);
	CHECK(scripted.replace_call_count == 0);
	CHECK(scripted.create_new_call_count == 0);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage publish failures preserve the old or absent target")
{
	VegetationTest::ScopedAssetRoot root("storage-publish-failures");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	ScriptedCommitFileOps scripted(default_ops);

	SUBCASE("failed replace preserves the existing Layer")
	{
		const std::filesystem::path target_relative =
			"vegetation/existing.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 71,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		scripted.replace = [](const std::filesystem::path&, const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::TargetPreserved;
			result.error = "injected target-preserved replace failure";
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 71, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK(scripted.replace_call_count == 1);
		CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("partial replace recovery failure retains the exact recovery backup")
	{
		const std::filesystem::path target_relative =
			"vegetation/recovery-required.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 73,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path recovery_backup = target_absolute.parent_path() /
			".ashveg-layer-stage-replace-backup-injected.tmp";
		const AshEngine::VegetationFileInspection target_before_recovery =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_before_recovery.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_before_recovery.file_identity.available);
		scripted.replace = [&](const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			REQUIRE(registry.BeginStageFilePublish(stage, target));
			REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
				recovery_backup, stage, target,
				target_before_recovery.file_identity));
			std::error_code move_error{};
			std::filesystem::rename(target, recovery_backup, move_error);
			REQUIRE_FALSE(move_error);
			REQUIRE(registry.ResolveStageFilePublish(stage,
				AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = recovery_backup;
			result.error = "injected restore failure";
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 73, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(committed.recovery_path == recovery_backup);
		CHECK_FALSE(std::filesystem::exists(target_absolute));
		CHECK(VegetationTest::ReadAllBytes(recovery_backup) == original);
		CHECK(std::filesystem::exists(prepared.stage_path()));
		CHECK(cleanup_registry.IsRecoveryStageFile(recovery_backup));
		CHECK(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			recovery_backup, prepared.stage_path(), target_absolute,
			target_before_recovery.file_identity));
		const AshEngine::VegetationOwnedStageCleanupStatus retained =
			cleanup_registry.RetryAll(default_ops);
		CHECK_FALSE(retained.all_removed);
		CHECK(std::filesystem::exists(recovery_backup));
		CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));

		std::error_code restore_error{};
		std::filesystem::rename(recovery_backup, target_absolute, restore_error);
		REQUIRE_FALSE(restore_error);
		REQUIRE(cleanup_registry.ReleaseRecoveryStageFile(recovery_backup));
		REQUIRE(cleanup_registry.ForgetConsumedStageFile(recovery_backup));
		CHECK(cleanup_registry.empty());
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	}

	SUBCASE("same-byte replacement of an associated recovery backup is rejected")
	{
		const std::filesystem::path target_relative =
			"vegetation/recovery-backup-native-identity-drift.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement =
			VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 7301,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path recovery_backup = target_absolute.parent_path() /
			".ashveg-layer-stage-replace-backup-native-identity-drift.tmp";
		const AshEngine::VegetationFileInspection target_before_recovery =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_before_recovery.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_before_recovery.file_identity.available);

		AshEngine::VegetationStageFileResult same_bytes_replacement =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 730101);
		REQUIRE(same_bytes_replacement.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(same_bytes_replacement.file_identity.available);
		REQUIRE(same_bytes_replacement.writer->WriteBlock(
			0, { original.data(), original.size() }));
		REQUIRE(same_bytes_replacement.writer->FlushAndClose());
		same_bytes_replacement.writer.reset();
		REQUIRE((same_bytes_replacement.file_identity.volume_serial_number !=
			target_before_recovery.file_identity.volume_serial_number ||
			same_bytes_replacement.file_identity.file_index !=
				target_before_recovery.file_identity.file_index));

		scripted.replace = [&](const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			REQUIRE(registry.BeginStageFilePublish(stage, target));
			REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
				recovery_backup, stage, target,
				target_before_recovery.file_identity));
			std::error_code move_error{};
			std::filesystem::rename(target, recovery_backup, move_error);
			REQUIRE_FALSE(move_error);
			REQUIRE(registry.ResolveStageFilePublish(stage,
				AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
			REQUIRE(default_ops.RemoveOwnedStageFile(
				recovery_backup, target_before_recovery.file_identity));
			std::filesystem::rename(
				same_bytes_replacement.owned_stage_file, recovery_backup, move_error);
			REQUIRE_FALSE(move_error);
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = recovery_backup;
			result.error = "injected recovery backup identity drift";
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 7301, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(committed.recovery_path == prepared.stage_path());
		CHECK(committed.recovery_path != recovery_backup);
		CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		CHECK_FALSE(cleanup_registry.OwnsStageFile(recovery_backup));
		const AshEngine::VegetationFileInspection replacement_after =
			default_ops.InspectPath(
				root.Path(), recovery_backup.lexically_relative(root.Path()));
		REQUIRE(replacement_after.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(replacement_after.file_identity.available);
		CHECK(replacement_after.file_identity.volume_serial_number ==
			same_bytes_replacement.file_identity.volume_serial_number);
		CHECK(replacement_after.file_identity.file_index ==
			same_bytes_replacement.file_identity.file_index);
		CHECK(VegetationTest::ReadAllBytes(recovery_backup) == original);

		if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		}
		if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		}
		if (cleanup_registry.IsRecoveryStageFile(recovery_backup))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(recovery_backup));
		}
		if (cleanup_registry.OwnsStageFile(recovery_backup))
		{
			CHECK(cleanup_registry.ForgetConsumedStageFile(recovery_backup));
		}
		CHECK(default_ops.RemoveOwnedStageFile(
			recovery_backup, same_bytes_replacement.file_identity));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("generic recovery state cannot masquerade as this atomic replace recovery")
	{
		const std::filesystem::path target_relative =
			"vegetation/generic-recovery-decoy.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 731,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path decoy_relative =
			"vegetation/.ashveg-layer-stage-generic-recovery-decoy.tmp";
		const std::filesystem::path decoy = root.Path() / decoy_relative;
		const std::vector<uint8_t> decoy_bytes{ 0x64, 0x65, 0x63, 0x6f, 0x79 };
		root.Write(decoy_relative, decoy_bytes);
		const AshEngine::VegetationFileInspection decoy_inspection =
			default_ops.InspectPath(root.Path(), decoy_relative);
		REQUIRE(decoy_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(decoy_inspection.file_identity.available);
		scripted.replace = [&](const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			REQUIRE(registry.TrackNewRecoveryStageFile(
				decoy, decoy_inspection.file_identity));
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = decoy;
			result.error = "injected generic recovery decoy";
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 731, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(committed.recovery_path == prepared.stage_path());
		CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		CHECK(std::filesystem::exists(prepared.stage_path()));
		CHECK(cleanup_registry.IsRecoveryStageFile(decoy));
		CHECK_FALSE(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			decoy, prepared.stage_path(), target_absolute,
			decoy_inspection.file_identity));
		CHECK_FALSE(cleanup_registry.CleanupStageFile(decoy, default_ops));
		CHECK(VegetationTest::ReadAllBytes(decoy) == decoy_bytes);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);

		if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		}
		if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		}
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(decoy));
		CHECK(cleanup_registry.CleanupStageFile(decoy, default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("another atomic operation recovery cannot masquerade as this recovery")
	{
		const std::filesystem::path target_relative =
			"vegetation/foreign-recovery-decoy.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 732,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);

		const std::filesystem::path other_source_relative =
			"vegetation/.ashveg-layer-stage-other-operation.tmp";
		const std::filesystem::path other_source = root.Path() / other_source_relative;
		const std::filesystem::path other_target =
			root.Path() / "vegetation/other-operation.AshVegetationLayer";
		const std::filesystem::path decoy_relative =
			"vegetation/.ashveg-layer-stage-replace-backup-other-operation.tmp";
		const std::filesystem::path decoy = root.Path() / decoy_relative;
		const std::vector<uint8_t> other_source_bytes{ 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65 };
		const std::vector<uint8_t> decoy_bytes{ 0x62, 0x61, 0x63, 0x6b, 0x75, 0x70 };
		root.Write(other_source_relative, other_source_bytes);
		root.Write(decoy_relative, decoy_bytes);
		const AshEngine::VegetationFileInspection other_source_inspection =
			default_ops.InspectPath(root.Path(), other_source_relative);
		const AshEngine::VegetationFileInspection decoy_inspection =
			default_ops.InspectPath(root.Path(), decoy_relative);
		REQUIRE(other_source_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(other_source_inspection.file_identity.available);
		REQUIRE(decoy_inspection.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(decoy_inspection.file_identity.available);
		scripted.replace = [&](const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			REQUIRE(registry.TrackStageFile(
				other_source, other_source_inspection.file_identity));
			REQUIRE(registry.BeginStageFilePublish(other_source, other_target));
			REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
				decoy, other_source, other_target,
				decoy_inspection.file_identity));
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			result.recovery_path = decoy;
			result.error = "injected foreign operation recovery decoy";
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 732, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(committed.recovery_path == prepared.stage_path());
		CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		CHECK(std::filesystem::exists(prepared.stage_path()));
		CHECK(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			decoy, other_source, other_target, decoy_inspection.file_identity));
		CHECK_FALSE(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			decoy, prepared.stage_path(), target_absolute,
			decoy_inspection.file_identity));
		CHECK_FALSE(cleanup_registry.CleanupStageFile(decoy, default_ops));
		CHECK(VegetationTest::ReadAllBytes(decoy) == decoy_bytes);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);

		if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		}
		if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		}
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(other_source));
		CHECK(cleanup_registry.CleanupStageFile(other_source, default_ops));
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(decoy));
		CHECK(cleanup_registry.CleanupStageFile(decoy, default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("illegal replace result shape retains the staged replacement for recovery")
	{
		const std::filesystem::path target_relative =
			"vegetation/illegal-replace-shape.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision,
				VegetationTest::MinimalLayerSnapshot(), 74,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		scripted.replace = [&](const std::filesystem::path&,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult result{};
			result.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
			result.recovery_path = target_absolute;
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 74, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
		CHECK(committed.recovery_path == prepared.stage_path());
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		CHECK(std::filesystem::exists(prepared.stage_path()));
		REQUIRE(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("post-consume corrupt replace result never invents a missing recovery stage")
	{
		const std::filesystem::path target_relative =
			"vegetation/post-consume-corrupt.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		root.Write(target_relative, VegetationTest::MinimalLayerBytes());
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		const std::vector<uint8_t> replacement_bytes = EncodeLayerOrThrow(replacement);
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 75,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path consumed_stage = prepared.stage_path();
		scripted.replace = [&](const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			AshEngine::VegetationAtomicReplaceResult result =
				default_ops.AtomicReplace(stage, target, registry);
			REQUIRE(result.status == AshEngine::VegetationAtomicReplaceStatus::Replaced);
			result.recovery_path = stage;
			return result;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 75, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.recovery_path.empty());
		CHECK_FALSE(std::filesystem::exists(consumed_stage));
		CHECK_FALSE(cleanup_registry.OwnsStageFile(consumed_stage));
		CHECK(cleanup_registry.empty());
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == replacement_bytes);
	}

	SUBCASE("post-consume invalid recovery ownership never recreates a missing stage entry")
	{
		const std::filesystem::path target_relative =
			"vegetation/post-consume-invalid-recovery.AshVegetationLayer";
		root.Write(target_relative, VegetationTest::MinimalLayerBytes());
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 76,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path consumed_stage = prepared.stage_path();
		scripted.replace = [&](const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			const AshEngine::VegetationAtomicReplaceResult replaced =
				default_ops.AtomicReplace(stage, target, registry);
			REQUIRE(replaced.status == AshEngine::VegetationAtomicReplaceStatus::Replaced);
			AshEngine::VegetationAtomicReplaceResult corrupt{};
			corrupt.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
			corrupt.recovery_path = stage;
			return corrupt;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_write(
				prepared, 76, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.recovery_path.empty());
		CHECK_FALSE(std::filesystem::exists(consumed_stage));
		CHECK_FALSE(cleanup_registry.OwnsStageFile(consumed_stage));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("failed create-new preserves an absent destination")
	{
		const std::filesystem::path target_relative =
			"vegetation/new.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_copy_as(
				root.Path(), target_relative, VegetationTest::MinimalLayerSnapshot(), 72,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		scripted.create_new = [](const std::filesystem::path&, const std::filesystem::path&)
		{
			return AshEngine::VegetationCreateNewStatus::Failed;
		};

		const AshEngine::VegetationStorageResult committed =
			AshEngine::commit_vegetation_layer_copy_as(
				prepared, 72, VegetationTest::ActiveControl(std::chrono::seconds(1)),
				cleanup_registry, scripted);
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(std::filesystem::exists(target_absolute));
		CHECK(scripted.create_new_call_count == 1);
		CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation storage converts an AtomicReplace exception after pinning into recovery")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-pin-throw");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/pin-throw.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	const std::vector<uint8_t> replacement_bytes = EncodeLayerOrThrow(replacement);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 733,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		-> AshEngine::VegetationAtomicReplaceResult
	{
		REQUIRE(registry.BeginStageFilePublish(stage, target));
		throw std::runtime_error("injected exception after publish pin");
	};

	AshEngine::VegetationStorageResult committed{};
	CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
		prepared, 733, VegetationTest::ActiveControl(std::chrono::seconds(1)),
		cleanup_registry, scripted));
	CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
	CHECK(committed.recovery_path == prepared.stage_path());
	CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
	const AshEngine::VegetationFileInspection retained_stage =
		default_ops.InspectPath(
			root.Path(), prepared.stage_path().lexically_relative(root.Path()));
	REQUIRE(retained_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(retained_stage.file_identity.available);
	CHECK(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
		prepared.stage_path(), prepared.stage_path(), target_absolute,
		retained_stage.file_identity));
	CHECK(VegetationTest::ReadAllBytes(prepared.stage_path()) == replacement_bytes);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);

	if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
	}
	if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
	}
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage contains exceptions while proving post-replace stage evidence")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-evidence-throw");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/evidence-throw.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	replacement.layer_seed ^= 0x51a9ull;

	SUBCASE("a publishing pin remains protected when exception reconciliation cannot read the stage")
	{
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 739,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		ScriptedCommitFileOps scripted(default_ops);
		scripted.read = [&](const std::filesystem::path& path, const uint64_t max_bytes)
		{
			if (path == prepared.stage_path() && scripted.read_call_count == 3)
			{
				throw std::runtime_error("injected post-replace stage evidence read failure");
			}
			return default_ops.ReadAllBytes(path, max_bytes);
		};
		scripted.replace = [](const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
			-> AshEngine::VegetationAtomicReplaceResult
		{
			REQUIRE(registry.BeginStageFilePublish(stage, target));
			throw std::runtime_error("injected exception after publish pin");
		};

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
			prepared, 739, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.recovery_path.empty());
		CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		const AshEngine::VegetationFileInspection retained_stage =
			default_ops.InspectPath(
				root.Path(), prepared.stage_path().lexically_relative(root.Path()));
		REQUIRE(retained_stage.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(retained_stage.file_identity.available);
		CHECK(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
			prepared.stage_path(), prepared.stage_path(), target_absolute,
			retained_stage.file_identity));
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK(std::filesystem::exists(prepared.stage_path()));

		CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		CHECK(cleanup_registry.empty());
	}

	SUBCASE("an owned stage proof failure is reported without escaping")
	{
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement, 740,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		ScriptedCommitFileOps scripted(default_ops);
		scripted.read = [&](const std::filesystem::path& path, const uint64_t max_bytes)
		{
			if (path == prepared.stage_path() && scripted.read_call_count == 3)
			{
				throw std::runtime_error("injected intact-stage proof read failure");
			}
			return default_ops.ReadAllBytes(path, max_bytes);
		};
		scripted.replace = [](const std::filesystem::path& stage,
			const std::filesystem::path&,
			AshEngine::VegetationOwnedStageCleanupRegistry&)
		{
			AshEngine::VegetationAtomicReplaceResult illegal{};
			illegal.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
			illegal.recovery_path = stage;
			return illegal;
		};

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
			prepared, 740, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.recovery_path.empty());
		CHECK_FALSE(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
		CHECK(cleanup_registry.OwnsStageFile(prepared.stage_path()));
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK(std::filesystem::exists(prepared.stage_path()));

		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation storage contains pre-publication commit provider exceptions")
{
	enum class Fault : uint8_t
	{
		AcquireLease = 0,
		InspectTarget,
		ReadSource,
		ReadStage
	};
	struct Case
	{
		const char* name = nullptr;
		Fault fault = Fault::AcquireLease;
	};
	const std::array<Case, 4> cases{{
		{ "lease", Fault::AcquireLease },
		{ "target inspection", Fault::InspectTarget },
		{ "source read", Fault::ReadSource },
		{ "stage read", Fault::ReadStage }
	}};

	for (size_t case_index = 0; case_index < cases.size(); ++case_index)
	{
		const Case& test_case = cases[case_index];
		CAPTURE(test_case.name);
		VegetationTest::ScopedAssetRoot root(
			"storage-prepublish-throw-" + std::to_string(case_index));
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		const std::filesystem::path target_relative =
			"vegetation/prepublish-throw.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
		root.Write(target_relative, original);
		const AshEngine::VegetationLayerReadResult opened =
			AshEngine::read_vegetation_layer_snapshot(
				root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
		REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
		AshEngine::VegetationLayerSnapshot replacement =
			VegetationTest::MinimalLayerSnapshot();
		++replacement.content_generation;
		replacement.layer_seed ^= 0x7400ull + case_index;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const uint64_t operation_serial = 741 + case_index;
		const AshEngine::VegetationPreparedLayerWrite prepared =
			AshEngine::prepare_vegetation_layer_write(
				root.Path(), target_relative, opened.revision, replacement,
				operation_serial,
				VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path stage = prepared.stage_path();
		ScriptedCommitFileOps scripted(default_ops);
		if (test_case.fault == Fault::AcquireLease)
		{
			scripted.acquire = [](std::string_view,
				const AshEngine::VegetationOperationControl&)
				-> AshEngine::VegetationFileLeaseResult
			{
				throw std::runtime_error("injected lease exception");
			};
		}
		if (test_case.fault == Fault::InspectTarget)
		{
			scripted.inspect = [](const std::filesystem::path&,
				const std::filesystem::path&) -> AshEngine::VegetationFileInspection
			{
				throw std::runtime_error("injected inspection exception");
			};
		}
		if (test_case.fault == Fault::ReadSource ||
			test_case.fault == Fault::ReadStage)
		{
			scripted.read = [&](const std::filesystem::path& path,
				const uint64_t max_bytes)
			{
				const bool inject_source = test_case.fault == Fault::ReadSource &&
					path == target_absolute;
				const bool inject_stage = test_case.fault == Fault::ReadStage &&
					path == stage && scripted.read_call_count == 2;
				if (inject_source || inject_stage)
				{
					throw std::runtime_error("injected pre-publication read exception");
				}
				return default_ops.ReadAllBytes(path, max_bytes);
			};
		}

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
			prepared, operation_serial,
			VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.recovery_path.empty());
		CHECK(scripted.replace_call_count == 0);
		CHECK(scripted.create_new_call_count == 0);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
		CHECK_FALSE(std::filesystem::exists(stage));
		CHECK_FALSE(cleanup_registry.OwnsStageFile(stage));
		CHECK(cleanup_registry.empty());
	}
}

TEST_CASE("Vegetation storage preserves terminal replace success when its callback throws")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-success-throw");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/success-throw.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	replacement.layer_seed ^= 0x7337ull;
	const std::vector<uint8_t> replacement_bytes = EncodeLayerOrThrow(replacement);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 734,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		-> AshEngine::VegetationAtomicReplaceResult
	{
		const AshEngine::VegetationAtomicReplaceResult replaced =
			default_ops.AtomicReplace(stage, target, registry);
		REQUIRE(replaced.status == AshEngine::VegetationAtomicReplaceStatus::Replaced);
		throw std::runtime_error("injected exception after terminal replace success");
	};

	AshEngine::VegetationStorageResult committed{};
	CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
		prepared, 734, VegetationTest::ActiveControl(std::chrono::seconds(1)),
		cleanup_registry, scripted));
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	CHECK(committed.recovery_path.empty());
	CHECK(committed.resulting_revision.has_value());
	if (committed.resulting_revision.has_value())
	{
		CHECK(*committed.resulting_revision == prepared.staged_revision());
	}
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK_FALSE(cleanup_registry.OwnsStageFile(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == replacement_bytes);
	const AshEngine::VegetationLayerReadResult reloaded =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(reloaded.status == AshEngine::VegetationStorageStatus::Succeeded);
	CHECK(reloaded.revision == prepared.staged_revision());
	REQUIRE(reloaded.snapshot != nullptr);
	CHECK(reloaded.snapshot->content_generation == replacement.content_generation);
	CHECK(reloaded.snapshot->layer_seed == replacement.layer_seed);

	if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
		{
			CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
		}
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
	}
}

TEST_CASE("Vegetation storage rejects a same-byte replacement of its prepared stage")
{
	VegetationTest::ScopedAssetRoot root("storage-stage-native-identity-drift");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/stage-native-identity-drift.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 735,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	const std::vector<uint8_t> staged_bytes =
		VegetationTest::ReadAllBytes(prepared.stage_path());
	const std::filesystem::path stage_relative =
		prepared.stage_path().lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection original_stage =
		default_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(original_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(original_stage.exists);
	REQUIRE(original_stage.is_regular_file);
	REQUIRE(original_stage.file_identity.available);

	AshEngine::VegetationStageFileResult same_bytes_replacement =
		default_ops.CreateUniqueSiblingStageFile(target_absolute, 73501);
	REQUIRE(same_bytes_replacement.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(same_bytes_replacement.file_identity.available);
	REQUIRE((same_bytes_replacement.file_identity.volume_serial_number !=
		original_stage.file_identity.volume_serial_number ||
		same_bytes_replacement.file_identity.file_index !=
			original_stage.file_identity.file_index));
	REQUIRE(same_bytes_replacement.writer->WriteBlock(
		0, { staged_bytes.data(), staged_bytes.size() }));
	REQUIRE(same_bytes_replacement.writer->FlushAndClose());
	same_bytes_replacement.writer.reset();
	REQUIRE(default_ops.RemoveOwnedStageFile(
		prepared.stage_path(), original_stage.file_identity));
	std::error_code replace_error{};
	std::filesystem::rename(same_bytes_replacement.owned_stage_file,
		prepared.stage_path(), replace_error);
	REQUIRE_FALSE(replace_error);
	const AshEngine::VegetationFileInspection recreated_stage =
		default_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(recreated_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(recreated_stage.file_identity.available);
	CHECK(recreated_stage.file_identity.volume_serial_number ==
		same_bytes_replacement.file_identity.volume_serial_number);
	CHECK(recreated_stage.file_identity.file_index ==
		same_bytes_replacement.file_identity.file_index);
	CHECK(VegetationTest::ReadAllBytes(prepared.stage_path()) == staged_bytes);

	ScriptedCommitFileOps scripted(default_ops);
	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 735, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(committed.recovery_path.empty());
	CHECK(scripted.replace_call_count == 0);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);

	if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
	}
	if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ForgetConsumedStageFile(prepared.stage_path()));
	}
	CHECK(default_ops.RemoveOwnedStageFile(
		prepared.stage_path(), same_bytes_replacement.file_identity));
}

TEST_CASE("Vegetation storage never infers replace success from an unrelated target identity")
{
	VegetationTest::ScopedAssetRoot root("storage-unrelated-target-success-proof");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/unrelated-target-success-proof.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	replacement.layer_seed ^= 0x735ull;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 736,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	const std::vector<uint8_t> staged_bytes =
		VegetationTest::ReadAllBytes(prepared.stage_path());
	const std::filesystem::path stage_relative =
		prepared.stage_path().lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection original_stage =
		default_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(original_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(original_stage.file_identity.available);
	AshEngine::VegetationFileIdentity forged_target_identity{};

	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		-> AshEngine::VegetationAtomicReplaceResult
	{
		AshEngine::VegetationStageFileResult forged_target =
			default_ops.CreateUniqueSiblingStageFile(target, 73601);
		REQUIRE(forged_target.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(forged_target.file_identity.available);
		REQUIRE((forged_target.file_identity.volume_serial_number !=
			original_stage.file_identity.volume_serial_number ||
			forged_target.file_identity.file_index != original_stage.file_identity.file_index));
		REQUIRE(forged_target.writer->WriteBlock(
			0, { staged_bytes.data(), staged_bytes.size() }));
		REQUIRE(forged_target.writer->FlushAndClose());
		forged_target.writer.reset();
		forged_target_identity = forged_target.file_identity;
		REQUIRE(default_ops.RemoveOwnedStageFile(
			stage, original_stage.file_identity));
		std::error_code remove_error{};
		REQUIRE(std::filesystem::remove(target, remove_error));
		REQUIRE_FALSE(remove_error);
		std::filesystem::rename(forged_target.owned_stage_file, target, remove_error);
		REQUIRE_FALSE(remove_error);
		REQUIRE(registry.ForgetConsumedStageFile(stage));
		throw std::runtime_error("injected exception after unrelated target creation");
	};

	AshEngine::VegetationStorageResult committed{};
	CHECK_NOTHROW(committed = AshEngine::commit_vegetation_layer_write(
		prepared, 736, VegetationTest::ActiveControl(std::chrono::seconds(1)),
		cleanup_registry, scripted));
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK_FALSE(committed.resulting_revision.has_value());
	CHECK(committed.recovery_path.empty());
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == staged_bytes);
	const AshEngine::VegetationFileInspection forged_target =
		default_ops.InspectPath(root.Path(), target_relative);
	REQUIRE(forged_target.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(forged_target.file_identity.available);
	CHECK(forged_target.file_identity.volume_serial_number ==
		forged_target_identity.volume_serial_number);
	CHECK(forged_target.file_identity.file_index == forged_target_identity.file_index);
	CHECK((forged_target.file_identity.volume_serial_number !=
		original_stage.file_identity.volume_serial_number ||
		forged_target.file_identity.file_index != original_stage.file_identity.file_index));
}

TEST_CASE("Vegetation storage rejects exact-stage recovery after native identity loss")
{
	VegetationTest::ScopedAssetRoot root("storage-exact-recovery-native-identity");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/exact-recovery-native-identity.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 737,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	const std::vector<uint8_t> staged_bytes =
		VegetationTest::ReadAllBytes(prepared.stage_path());
	const std::filesystem::path stage_relative =
		prepared.stage_path().lexically_relative(root.Path());
	const AshEngine::VegetationFileInspection original_stage =
		default_ops.InspectPath(root.Path(), stage_relative);
	REQUIRE(original_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(original_stage.file_identity.available);

	bool recreate_stage = false;
	SUBCASE("missing exact recovery stage")
	{
	}
	SUBCASE("same-byte exact recovery stage with a different native identity")
	{
		recreate_stage = true;
	}
	std::filesystem::path replacement_stage_path{};
	AshEngine::VegetationFileIdentity replacement_stage_identity{};
	if (recreate_stage)
	{
		AshEngine::VegetationStageFileResult identity_replacement =
			default_ops.CreateUniqueSiblingStageFile(target_absolute, 73701);
		REQUIRE(identity_replacement.status ==
			AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(identity_replacement.file_identity.available);
		REQUIRE((identity_replacement.file_identity.volume_serial_number !=
			original_stage.file_identity.volume_serial_number ||
			identity_replacement.file_identity.file_index !=
				original_stage.file_identity.file_index));
		REQUIRE(identity_replacement.writer->WriteBlock(
			0, { staged_bytes.data(), staged_bytes.size() }));
		REQUIRE(identity_replacement.writer->FlushAndClose());
		identity_replacement.writer.reset();
		replacement_stage_path = identity_replacement.owned_stage_file;
		replacement_stage_identity = identity_replacement.file_identity;
	}

	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		REQUIRE(registry.BeginStageFilePublish(stage, target));
		REQUIRE(registry.ResolveStageFilePublish(
			stage, AshEngine::VegetationStageFilePublishResolution::RecoveryRequired));
		REQUIRE(default_ops.RemoveOwnedStageFile(
			stage, original_stage.file_identity));
		if (recreate_stage)
		{
			std::error_code recreate_error{};
			std::filesystem::rename(replacement_stage_path, stage, recreate_error);
			REQUIRE_FALSE(recreate_error);
		}
		AshEngine::VegetationAtomicReplaceResult result{};
		result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		result.recovery_path = stage;
		result.error = "injected exact-stage identity loss";
		return result;
	};

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 737, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(committed.recovery_path.empty());
	CHECK(scripted.replace_call_count == 1);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	CHECK(cleanup_registry.empty());
	if (recreate_stage)
	{
		const AshEngine::VegetationFileInspection recreated_stage =
			default_ops.InspectPath(root.Path(), stage_relative);
		REQUIRE(recreated_stage.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(recreated_stage.file_identity.available);
		CHECK(recreated_stage.file_identity.volume_serial_number ==
			replacement_stage_identity.volume_serial_number);
		CHECK(recreated_stage.file_identity.file_index ==
			replacement_stage_identity.file_index);
		CHECK(VegetationTest::ReadAllBytes(prepared.stage_path()) == staged_bytes);
	}
	else
	{
		CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	}

	if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
	}
	if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ForgetConsumedStageFile(prepared.stage_path()));
	}
	const AshEngine::VegetationFileIdentity final_stage_identity =
		recreate_stage ? replacement_stage_identity : original_stage.file_identity;
	CHECK(default_ops.RemoveOwnedStageFile(
		prepared.stage_path(), final_stage_identity));
}

TEST_CASE("Vegetation storage rejects invalid associated replace backups")
{
	VegetationTest::ScopedAssetRoot root("storage-invalid-associated-backup");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/invalid-associated-backup.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 738,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);

	std::vector<uint8_t> backup_bytes{};
	SUBCASE("corrupt associated backup")
	{
		backup_bytes = { 0x62, 0x61, 0x64 };
	}
	SUBCASE("valid associated backup from the wrong old revision")
	{
		backup_bytes = VegetationTest::DifferentValidLayerBytes();
		REQUIRE(backup_bytes != original);
	}
	const std::filesystem::path backup = target_absolute.parent_path() /
		".ashveg-layer-stage-replace-backup-invalid-old-revision.tmp";
	VegetationTest::WriteAllBytes(backup, backup_bytes);
	const AshEngine::VegetationFileInspection backup_inspection =
		default_ops.InspectPath(
			root.Path(), backup.lexically_relative(root.Path()));
	REQUIRE(backup_inspection.status ==
		AshEngine::VegetationFileResultStatus::Succeeded);
	REQUIRE(backup_inspection.file_identity.available);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		REQUIRE(registry.BeginStageFilePublish(stage, target));
		REQUIRE(registry.RetainStageFileForAtomicReplaceRecovery(
			backup, stage, target, backup_inspection.file_identity));
		REQUIRE(registry.ResolveStageFilePublish(
			stage, AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
		AshEngine::VegetationAtomicReplaceResult result{};
		result.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		result.recovery_path = backup;
		result.error = "injected invalid associated backup";
		return result;
	};

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 738, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
	CHECK(committed.recovery_path == prepared.stage_path());
	CHECK(cleanup_registry.IsRecoveryStageFile(prepared.stage_path()));
	CHECK(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.IsAtomicReplaceRecoveryStageFile(
		backup, prepared.stage_path(), target_absolute,
		backup_inspection.file_identity));
	CHECK_FALSE(cleanup_registry.CleanupStageFile(backup, default_ops));
	CHECK(VegetationTest::ReadAllBytes(backup) == backup_bytes);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);

	if (cleanup_registry.IsRecoveryStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.ReleaseRecoveryStageFile(prepared.stage_path()));
	}
	if (cleanup_registry.OwnsStageFile(prepared.stage_path()))
	{
		CHECK(cleanup_registry.CleanupStageFile(prepared.stage_path(), default_ops));
	}
	CHECK(cleanup_registry.ReleaseRecoveryStageFile(backup));
	CHECK(cleanup_registry.CleanupStageFile(backup, default_ops));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage rejects every illegal atomic replace status payload shape")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-shape-matrix");
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationOwnedStageCleanupRegistry registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision,
			VegetationTest::MinimalLayerSnapshot(), 77,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	AshEngine::VegetationAtomicReplaceResult injected{};
	bool leave_publish_pin = false;
	SUBCASE("Replaced cannot carry recovery")
	{
		injected.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
		injected.recovery_path = prepared.stage_path();
	}
	SUBCASE("TargetPreserved cannot carry recovery")
	{
		injected.status = AshEngine::VegetationAtomicReplaceStatus::TargetPreserved;
		injected.recovery_path = prepared.stage_path();
	}
	SUBCASE("RecoveryRequired cannot omit recovery")
	{
		injected.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		injected.recovery_path.clear();
	}
	SUBCASE("RecoveryRequired cannot carry a relative recovery path")
	{
		injected.status = AshEngine::VegetationAtomicReplaceStatus::RecoveryRequired;
		injected.recovery_path = "relative-stage.tmp";
	}
	SUBCASE("illegal shape after an unresolved publish pin retains the protected stage")
	{
		injected.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
		injected.recovery_path = prepared.stage_path();
		leave_publish_pin = true;
	}
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& replace_registry)
	{
		if (leave_publish_pin)
		{
			REQUIRE(replace_registry.BeginStageFilePublish(stage, target));
		}
		return injected;
	};

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 77, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::RecoveryRequired);
	CHECK(committed.recovery_path == prepared.stage_path());
	CHECK(registry.IsRecoveryStageFile(prepared.stage_path()));
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	REQUIRE(registry.ReleaseRecoveryStageFile(prepared.stage_path()));
	CHECK(registry.CleanupStageFile(prepared.stage_path(), default_ops));
}

TEST_CASE("Vegetation storage rejects staged-byte tampering before publish")
{
	VegetationTest::ScopedAssetRoot root("storage-stage-tamper");
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	const std::vector<uint8_t> original = VegetationTest::MinimalLayerBytes();
	root.Write(target_relative, original);
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 73,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	VegetationTest::WriteAllBytes(
		prepared.stage_path(), VegetationTest::DifferentValidLayerBytes());

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 73, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == original);
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(cleanup_registry.empty());
}

TEST_CASE("Vegetation storage checked save reloads the committed revision and snapshot")
{
	VegetationTest::ScopedAssetRoot root("storage-checked-save-reload");
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	const std::filesystem::path target_relative =
		"vegetation/meadow.AshVegetationLayer";
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	replacement.layer_seed ^= 0x55aa55aa55aa55aaull;
	const std::vector<uint8_t> expected_bytes = EncodeLayerOrThrow(replacement);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 81,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		AshEngine::VegetationAtomicReplaceResult result =
			default_ops.AtomicReplace(stage, target, registry);
		if (result.status == AshEngine::VegetationAtomicReplaceStatus::Replaced)
		{
			CHECK(registry.ForgetConsumedStageFile(stage));
		}
		return result;
	};
	const AshEngine::VegetationOperationControl control =
		VegetationTest::ActiveControl(std::chrono::seconds(1));

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 81, control, cleanup_registry, scripted);
	REQUIRE(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(committed.resulting_revision.has_value());
	CHECK(scripted.last_lease_identity == opened.canonical_identity);
	CHECK(std::none_of(scripted.last_lease_identity.begin(),
		scripted.last_lease_identity.end(), [](const char value)
		{
			return value >= 'A' && value <= 'Z';
		}));
	CHECK(scripted.last_lease_cancel_requested == control.cancel_requested);
	CHECK(scripted.last_lease_deadline == control.deadline);
	CHECK(scripted.replace_call_count == 1);
	CHECK(cleanup_registry.empty());

	const AshEngine::VegetationLayerReadResult reloaded =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(reloaded.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(reloaded.snapshot != nullptr);
	CHECK(reloaded.revision == *committed.resulting_revision);
	CHECK(EncodeLayerOrThrow(*reloaded.snapshot) == expected_bytes);
	CHECK(VegetationTest::ReadAllBytes(root.Path() / target_relative) == expected_bytes);
}

TEST_CASE("Vegetation storage successful Copy As reloads the copy without changing source")
{
	VegetationTest::ScopedAssetRoot root("storage-copy-success-reload");
	const std::filesystem::path source_relative =
		"vegetation/source.AshVegetationLayer";
	const std::filesystem::path destination_relative =
		"vegetation/copy.AshVegetationLayer";
	const std::vector<uint8_t> source_bytes = VegetationTest::MinimalLayerBytes();
	root.Write(source_relative, source_bytes);
	AshEngine::VegetationLayerSnapshot copy_snapshot = VegetationTest::MinimalLayerSnapshot();
	++copy_snapshot.content_generation;
	copy_snapshot.layer_seed ^= 0xa5a5a5a5a5a5a5a5ull;
	const std::vector<uint8_t> expected_copy_bytes = EncodeLayerOrThrow(copy_snapshot);
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), destination_relative, copy_snapshot, 82,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	CHECK(prepared.canonical_relative_path() == destination_relative);

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_copy_as(
			prepared, 82, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry);
	REQUIRE(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(committed.resulting_revision.has_value());
	CHECK(cleanup_registry.empty());
	CHECK(VegetationTest::ReadAllBytes(root.Path() / source_relative) == source_bytes);

	const AshEngine::VegetationLayerReadResult reloaded =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), destination_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(reloaded.status == AshEngine::VegetationStorageStatus::Succeeded);
	REQUIRE(reloaded.snapshot != nullptr);
	CHECK(reloaded.revision == *committed.resulting_revision);
	CHECK(EncodeLayerOrThrow(*reloaded.snapshot) == expected_copy_bytes);
	CHECK(VegetationTest::ReadAllBytes(root.Path() / destination_relative) ==
		expected_copy_bytes);
}

TEST_CASE("Vegetation storage create-new success is not downgraded by consumed bookkeeping")
{
	VegetationTest::ScopedAssetRoot root("storage-create-new-bookkeeping");
	const std::filesystem::path destination_relative =
		"vegetation/copy.AshVegetationLayer";
	AshEngine::VegetationLayerSnapshot snapshot = VegetationTest::MinimalLayerSnapshot();
	++snapshot.content_generation;
	const std::vector<uint8_t> expected_bytes = EncodeLayerOrThrow(snapshot);
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_copy_as(
			root.Path(), destination_relative, snapshot, 83,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.create_new = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target)
	{
		const AshEngine::VegetationCreateNewStatus created =
			default_ops.CreateNewFromStage(stage, target);
		REQUIRE(created == AshEngine::VegetationCreateNewStatus::Created);
		REQUIRE(cleanup_registry.ForgetConsumedStageFile(stage));
		return created;
	};

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_copy_as(
			prepared, 83, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	CHECK(cleanup_registry.empty());
	CHECK(VegetationTest::ReadAllBytes(root.Path() / destination_relative) == expected_bytes);
}

TEST_CASE("Vegetation storage create-new APIs contain exceptions before target mutation")
{
	const std::array<CreateNewStorageApi, 2> apis = {
		CreateNewStorageApi::FirstSave, CreateNewStorageApi::CopyAs
	};
	for (size_t index = 0; index < apis.size(); ++index)
	{
		const CreateNewStorageApi api = apis[index];
		CAPTURE(std::string(CreateNewStorageApiName(api)));
		VegetationTest::ScopedAssetRoot root(
			"storage-create-new-throw-before-" + std::to_string(index));
		const std::filesystem::path target_relative =
			"vegetation/throw-before.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const uint64_t operation_serial = 850u + index;
		const AshEngine::VegetationPreparedLayerWrite prepared =
			PrepareCreateNewStorageWrite(
				api, root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), operation_serial,
				cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path stage = prepared.stage_path();
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		ScriptedCommitFileOps scripted(default_ops);
		std::vector<std::filesystem::path> removed_stages{};
		scripted.remove_stage = [&default_ops, &removed_stages](
			const std::filesystem::path& path,
			const AshEngine::VegetationFileIdentity& expected_identity)
		{
			removed_stages.push_back(path);
			return default_ops.RemoveOwnedStageFile(path, expected_identity);
		};
		scripted.create_new = [](
			const std::filesystem::path&,
			const std::filesystem::path&) -> AshEngine::VegetationCreateNewStatus
		{
			throw std::runtime_error("injected create-new exception before mutation");
		};

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = CommitCreateNewStorageWrite(
			api, prepared, operation_serial, cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK_FALSE(committed.resulting_revision.has_value());
		CHECK(committed.recovery_path.empty());
		CHECK(scripted.create_new_call_count == 1);
		CHECK(removed_stages == std::vector<std::filesystem::path>{ stage });
		CHECK_FALSE(std::filesystem::exists(target_absolute));
		CHECK_FALSE(std::filesystem::exists(stage));
		CHECK(cleanup_registry.empty());

		if (cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(cleanup_registry.CleanupStageFile(stage, default_ops));
		}
	}
}

TEST_CASE("Vegetation storage create-new APIs recover terminal success after callback exceptions")
{
	const std::array<CreateNewStorageApi, 2> apis = {
		CreateNewStorageApi::FirstSave, CreateNewStorageApi::CopyAs
	};
	for (size_t index = 0; index < apis.size(); ++index)
	{
		const CreateNewStorageApi api = apis[index];
		CAPTURE(std::string(CreateNewStorageApiName(api)));
		VegetationTest::ScopedAssetRoot root(
			"storage-create-new-throw-after-created-" + std::to_string(index));
		const std::filesystem::path target_relative =
			"vegetation/throw-after-created.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		AshEngine::VegetationLayerSnapshot snapshot =
			VegetationTest::MinimalLayerSnapshot();
		++snapshot.content_generation;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const uint64_t operation_serial = 860u + index;
		const AshEngine::VegetationPreparedLayerWrite prepared =
			PrepareCreateNewStorageWrite(
				api, root.Path(), target_relative, snapshot, operation_serial,
				cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path stage = prepared.stage_path();
		const std::filesystem::path stage_relative =
			stage.lexically_relative(root.Path());
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		const AshEngine::VegetationFileInspection stage_before =
			default_ops.InspectPath(root.Path(), stage_relative);
		REQUIRE(stage_before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(stage_before.exists);
		REQUIRE(stage_before.file_identity.available);
		const std::vector<uint8_t> staged_bytes =
			VegetationTest::ReadAllBytes(stage);
		ScriptedCommitFileOps scripted(default_ops);
		scripted.create_new = [&default_ops](
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

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = CommitCreateNewStorageWrite(
			api, prepared, operation_serial, cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
		CHECK(committed.resulting_revision.has_value());
		if (committed.resulting_revision.has_value())
		{
			CHECK(*committed.resulting_revision == prepared.staged_revision());
		}
		CHECK(committed.recovery_path.empty());
		CHECK(scripted.create_new_call_count == 1);
		CHECK(cleanup_registry.empty());
		CHECK_FALSE(std::filesystem::exists(stage));
		const AshEngine::VegetationFileInspection target_after =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_after.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_after.exists);
		REQUIRE(target_after.is_regular_file);
		REQUIRE(target_after.file_identity.available);
		CHECK(target_after.resolved_absolute_path == target_absolute);
		CHECK(target_after.file_identity.volume_serial_number ==
			stage_before.file_identity.volume_serial_number);
		CHECK(target_after.file_identity.file_index ==
			stage_before.file_identity.file_index);
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == staged_bytes);

		if (cleanup_registry.OwnsStageFile(stage))
		{
			CHECK(cleanup_registry.ReconcileConsumedStageFileAfterPublish(
				stage, default_ops));
		}
	}
}

TEST_CASE("Vegetation storage create-new APIs reject unrelated exact-byte target identities")
{
	const std::array<CreateNewStorageApi, 2> apis = {
		CreateNewStorageApi::FirstSave, CreateNewStorageApi::CopyAs
	};
	for (size_t index = 0; index < apis.size(); ++index)
	{
		const CreateNewStorageApi api = apis[index];
		CAPTURE(std::string(CreateNewStorageApiName(api)));
		VegetationTest::ScopedAssetRoot root(
			"storage-create-new-throw-decoy-" + std::to_string(index));
		const std::filesystem::path target_relative =
			"vegetation/throw-decoy.AshVegetationLayer";
		const std::filesystem::path target_absolute = root.Path() / target_relative;
		AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
		const uint64_t operation_serial = 870u + index;
		const AshEngine::VegetationPreparedLayerWrite prepared =
			PrepareCreateNewStorageWrite(
				api, root.Path(), target_relative,
				VegetationTest::MinimalLayerSnapshot(), operation_serial,
				cleanup_registry);
		REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
		const std::filesystem::path stage = prepared.stage_path();
		const std::filesystem::path stage_relative =
			stage.lexically_relative(root.Path());
		AshEngine::IVegetationFileOps& default_ops =
			AshEngine::get_default_vegetation_file_ops();
		const AshEngine::VegetationFileInspection stage_before =
			default_ops.InspectPath(root.Path(), stage_relative);
		REQUIRE(stage_before.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(stage_before.file_identity.available);
		const std::vector<uint8_t> staged_bytes =
			VegetationTest::ReadAllBytes(stage);
		AshEngine::VegetationStageFileResult decoy =
			default_ops.CreateUniqueSiblingStageFile(
				target_absolute, operation_serial + 1000u);
		REQUIRE(decoy.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(decoy.file_identity.available);
		REQUIRE(decoy.writer->WriteBlock(
			0, { staged_bytes.data(), staged_bytes.size() }));
		REQUIRE(decoy.writer->FlushAndClose());
		decoy.writer.reset();
		const std::filesystem::path decoy_path = decoy.owned_stage_file;
		const AshEngine::VegetationFileIdentity decoy_identity = decoy.file_identity;
		ScriptedCommitFileOps scripted(default_ops);
		scripted.create_new =
			[&cleanup_registry, &default_ops, decoy_path, stage_before](
				const std::filesystem::path& source,
				const std::filesystem::path& target) -> AshEngine::VegetationCreateNewStatus
		{
			if (!default_ops.RemoveOwnedStageFile(
					source, stage_before.file_identity) ||
				default_ops.CreateNewFromStage(decoy_path, target) !=
					AshEngine::VegetationCreateNewStatus::Created ||
				!cleanup_registry.ForgetConsumedStageFile(source))
			{
				throw std::runtime_error("could not establish unrelated target");
			}
			throw std::runtime_error("injected exception after unrelated target creation");
		};

		AshEngine::VegetationStorageResult committed{};
		CHECK_NOTHROW(committed = CommitCreateNewStorageWrite(
			api, prepared, operation_serial, cleanup_registry, scripted));
		CHECK(committed.status == AshEngine::VegetationStorageStatus::Failed);
		CHECK(committed.status != AshEngine::VegetationStorageStatus::Succeeded);
		CHECK_FALSE(committed.resulting_revision.has_value());
		CHECK(committed.recovery_path.empty());
		CHECK(cleanup_registry.empty());
		CHECK_FALSE(std::filesystem::exists(stage));
		const AshEngine::VegetationFileInspection target_after =
			default_ops.InspectPath(root.Path(), target_relative);
		REQUIRE(target_after.status == AshEngine::VegetationFileResultStatus::Succeeded);
		REQUIRE(target_after.exists);
		REQUIRE(target_after.file_identity.available);
		CHECK(target_after.file_identity.volume_serial_number ==
			decoy_identity.volume_serial_number);
		CHECK(target_after.file_identity.file_index == decoy_identity.file_index);
		CHECK((target_after.file_identity.volume_serial_number !=
			stage_before.file_identity.volume_serial_number ||
			target_after.file_identity.file_index !=
				stage_before.file_identity.file_index));
		CHECK(VegetationTest::ReadAllBytes(target_absolute) == staged_bytes);
	}
}

TEST_CASE("Vegetation storage replace success reconciles a consumed publish pin")
{
	VegetationTest::ScopedAssetRoot root("storage-replace-publish-pin-reconcile");
	const std::filesystem::path target_relative =
		"vegetation/existing.AshVegetationLayer";
	const std::filesystem::path target_absolute = root.Path() / target_relative;
	root.Write(target_relative, VegetationTest::MinimalLayerBytes());
	const AshEngine::VegetationLayerReadResult opened =
		AshEngine::read_vegetation_layer_snapshot(
			root.Path(), target_relative, VegetationTest::GenerousLoadBudget());
	REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
	AshEngine::VegetationLayerSnapshot replacement = VegetationTest::MinimalLayerSnapshot();
	++replacement.content_generation;
	const std::vector<uint8_t> expected_bytes = EncodeLayerOrThrow(replacement);
	AshEngine::IVegetationFileOps& default_ops = AshEngine::get_default_vegetation_file_ops();
	AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
	const AshEngine::VegetationPreparedLayerWrite prepared =
		AshEngine::prepare_vegetation_layer_write(
			root.Path(), target_relative, opened.revision, replacement, 84,
			VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
	REQUIRE(prepared.status() == AshEngine::VegetationStorageStatus::Prepared);
	ScriptedCommitFileOps scripted(default_ops);
	scripted.replace = [&](const std::filesystem::path& stage,
		const std::filesystem::path& target,
		AshEngine::VegetationOwnedStageCleanupRegistry& registry)
	{
		REQUIRE(registry.BeginStageFilePublish(stage, target));
		std::error_code publish_error{};
		REQUIRE(std::filesystem::remove(target, publish_error));
		REQUIRE_FALSE(publish_error);
		std::filesystem::rename(stage, target, publish_error);
		REQUIRE_FALSE(publish_error);
		AshEngine::VegetationAtomicReplaceResult result{};
		result.status = AshEngine::VegetationAtomicReplaceStatus::Replaced;
		return result;
	};

	const AshEngine::VegetationStorageResult committed =
		AshEngine::commit_vegetation_layer_write(
			prepared, 84, VegetationTest::ActiveControl(std::chrono::seconds(1)),
			cleanup_registry, scripted);
	CHECK(committed.status == AshEngine::VegetationStorageStatus::Succeeded);
	CHECK(cleanup_registry.empty());
	CHECK_FALSE(std::filesystem::exists(prepared.stage_path()));
	CHECK(VegetationTest::ReadAllBytes(target_absolute) == expected_bytes);
}
