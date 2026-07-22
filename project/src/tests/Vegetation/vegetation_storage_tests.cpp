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

		bool RemoveOwnedStageFile(const std::filesystem::path& stage_file) override
		{
			removed_stage_files.push_back(stage_file);
			if (stage_file == failing_stage_file && remaining_remove_failures > 0)
			{
				--remaining_remove_failures;
				return false;
			}
			return m_backing.RemoveOwnedStageFile(stage_file);
		}

		bool RemoveOwnedStageTree(const std::filesystem::path& stage_root) override
		{
			removed_stage_trees.push_back(stage_root);
			return m_backing.RemoveOwnedStageTree(stage_root);
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
		std::function<bool(const std::filesystem::path&)> remove_stage{};
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

		bool RemoveOwnedStageFile(const std::filesystem::path& stage_file) override
		{
			++remove_stage_call_count;
			return remove_stage ? remove_stage(stage_file)
				: m_backing.RemoveOwnedStageFile(stage_file);
		}

		bool RemoveOwnedStageTree(const std::filesystem::path& stage_root) override
		{
			return m_backing.RemoveOwnedStageTree(stage_root);
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
	CHECK(file_ops.RemoveOwnedStageFile(stage.owned_stage_file));
	CHECK_FALSE(std::filesystem::exists(stage.owned_stage_file));
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
	REQUIRE(registry.TrackStageFile(stage.owned_stage_file));
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
	REQUIRE(registry.TrackStageFile(first.owned_stage_file));
	REQUIRE(registry.TrackStageFile(second.owned_stage_file));
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
	REQUIRE(registry.TrackStageFile(stage.owned_stage_file));
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
	REQUIRE(registry.TrackStageFile(stage.owned_stage_file));
	ScriptedCommitFileOps scripted(default_ops);
	std::mutex mutex{};
	std::condition_variable condition{};
	bool entered = false;
	bool release = false;
	scripted.remove_stage = [&](const std::filesystem::path& path)
	{
		{
			std::unique_lock<std::mutex> lock(mutex);
			entered = true;
			condition.notify_one();
			condition.wait(lock, [&] { return release; });
		}
		return default_ops.RemoveOwnedStageFile(path);
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
	REQUIRE(registry.TrackStageFile(stage.owned_stage_file));
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file));
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
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file));

	REQUIRE(registry.ResolveStageFilePublish(
		stage.owned_stage_file,
		AshEngine::VegetationStageFilePublishResolution::RecoveryRequired));
	CHECK(registry.IsRecoveryStageFile(stage.owned_stage_file));
	CHECK_FALSE(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	REQUIRE(registry.ReleaseRecoveryStageFile(stage.owned_stage_file));
	REQUIRE(registry.BeginStageFilePublish(stage.owned_stage_file));
	REQUIRE(registry.ResolveStageFilePublish(
		stage.owned_stage_file,
		AshEngine::VegetationStageFilePublishResolution::TargetPreserved));
	CHECK(registry.CleanupStageFile(stage.owned_stage_file, file_ops));
	CHECK(registry.empty());
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
		scripted.remove_stage = [](const std::filesystem::path&)
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
	REQUIRE(wrong_registry.TrackStageFile(prepared.stage_path()));

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
			CHECK(default_ops.RemoveOwnedStageFile(scripted.last_created_stage));
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
		REQUIRE(cleanup_registry.TrackStageFile(existing_stage));
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
		REQUIRE(cleanup_registry.TrackStageFile(existing_stage));
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
		CHECK(default_ops.RemoveOwnedStageFile(actual_stage));
		CHECK(default_ops.RemoveOwnedStageFile(reported_stage));
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
		REQUIRE(cleanup_registry.TrackStageFile(existing.owned_stage_file));
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
			".ashveg-layer-stage-recovery-injected.tmp";
		scripted.replace = [&](const std::filesystem::path&,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry& registry)
		{
			std::error_code move_error{};
			std::filesystem::rename(target, recovery_backup, move_error);
			REQUIRE_FALSE(move_error);
			REQUIRE(registry.RetainStageFileForRecovery(recovery_backup));
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
	scripted.replace = [&](const std::filesystem::path& stage, const std::filesystem::path&,
		AshEngine::VegetationOwnedStageCleanupRegistry& replace_registry)
	{
		if (leave_publish_pin)
		{
			REQUIRE(replace_registry.BeginStageFilePublish(stage));
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
		REQUIRE(registry.BeginStageFilePublish(stage));
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
