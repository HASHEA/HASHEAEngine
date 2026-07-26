#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/IEditorCommandExecutor.h"
#include "Services/VegetationEditorService.h"
#include "Services/VegetationEditorTaskExecutor.h"
#include "Vegetation/VegetationTestSupport.h"

#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	using namespace std::chrono_literals;

	struct Barrier
	{
		void Arrive()
		{
			std::lock_guard<std::mutex> lock(mutex);
			arrived = true;
			condition.notify_all();
		}

		void WaitForArrival()
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this] { return arrived; });
		}

		void Release()
		{
			std::lock_guard<std::mutex> lock(mutex);
			released = true;
			condition.notify_all();
		}

		void WaitForRelease()
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this] { return released; });
		}

		std::mutex mutex{};
		std::condition_variable condition{};
		bool arrived = false;
		bool released = false;
	};

	class ThrowingTaskExecutorAdapter final :
		public AshEditor::IVegetationEditorTaskExecutor
	{
	public:
		ThrowingTaskExecutorAdapter(
			AshEditor::IVegetationEditorTaskExecutor& inner,
			const bool wait_for_cancellation)
			: m_inner(inner)
			, m_wait_for_cancellation(wait_for_cancellation)
		{
		}

		uint64_t Submit(
			AshEditor::VegetationEditorTaskSubmission submission) override
		{
			submission.work =
				[wait_for_cancellation = m_wait_for_cancellation](
					const AshEngine::VegetationOperationControl control)
				{
					if (wait_for_cancellation)
					{
						while (control.cancel_requested &&
							!control.cancel_requested->load(
								std::memory_order_acquire))
						{
							std::this_thread::yield();
						}
					}
					throw std::runtime_error(
						"shutdown worker exception");
				};
			m_last_submitted_task = m_inner.Submit(std::move(submission));
			return m_last_submitted_task;
		}

		void RequestCancel(const uint64_t task_id) override
		{
			m_inner.RequestCancel(task_id);
		}

		bool IsComplete(const uint64_t task_id) const override
		{
			return m_inner.IsComplete(task_id);
		}

		std::exception_ptr GetException(const uint64_t task_id) const override
		{
			return m_inner.GetException(task_id);
		}

		void Join(const uint64_t task_id) override
		{
			m_inner.Join(task_id);
		}

		void CancelAndJoinAll() override
		{
			m_inner.CancelAndJoinAll();
		}

		bool IsIdle() const override
		{
			return m_inner.IsIdle();
		}

		uint64_t LastSubmittedTask() const
		{
			return m_last_submitted_task;
		}

	private:
		AshEditor::IVegetationEditorTaskExecutor& m_inner;
		bool m_wait_for_cancellation = false;
		uint64_t m_last_submitted_task = 0;
	};

	bool SameSurfaceIdentity(
		const AshEngine::VegetationSurfaceIdentity& lhs,
		const AshEngine::VegetationSurfaceIdentity& rhs)
	{
		return lhs.surface_id == rhs.surface_id &&
			lhs.content_revision == rhs.content_revision &&
			lhs.residency_revision == rhs.residency_revision &&
			lhs.transform_revision == rhs.transform_revision;
	}

	class ReadySurfaceSnapshot final :
		public AshEngine::IVegetationSurfaceSnapshot
	{
	public:
		enum class BatchMode : uint8_t
		{
			Ready = 0,
			Outside,
			Pending,
			Failed,
			Throw,
			Partial,
			WrongIndex
		};

		explicit ReadySurfaceSnapshot(
			AshEngine::VegetationSurfaceIdentity identity)
			: m_identity(identity)
		{
		}

		AshEngine::VegetationSurfaceIdentity identity() const override
		{
			++identity_call_count;
			if (throw_on_identity_call == identity_call_count)
			{
				throw std::runtime_error("identity failure");
			}
			return m_identity;
		}

		AshEngine::VegetationSurfaceBounds bounds() const override
		{
			return {
				AshEngine::VegetationChunkCoord{ -1024, -1024 },
				AshEngine::VegetationChunkCoord{ 1024, 1024 }
			};
		}

		AshEngine::VegetationSurfaceBatchResult sample_batch(
			const std::vector<AshEngine::VegetationSurfaceSampleRequest>& requests,
			AshEngine::VegetationOperationControl control) const override
		{
			batch_sizes.push_back(requests.size());
			batch_requests.push_back(requests);
			++batch_call_count;
			if (on_batch)
			{
				on_batch();
			}
			if (on_batch_control)
			{
				on_batch_control(control);
			}
			const BatchMode current_mode =
				fail_on_batch_call == 0 ||
					fail_on_batch_call == batch_call_count
				? mode
				: BatchMode::Ready;
			if (current_mode == BatchMode::Throw)
			{
				throw std::runtime_error("sample failure");
			}
			if (!control.cancel_requested ||
				control.cancel_requested->load(std::memory_order_acquire))
			{
				return {
					AshEngine::VegetationSurfaceStatus::Failed,
					{},
					"cancelled"
				};
			}

			AshEngine::VegetationSurfaceBatchResult result{};
			result.status =
				current_mode == BatchMode::Pending
					? AshEngine::VegetationSurfaceStatus::Pending
				: current_mode == BatchMode::Failed
					? AshEngine::VegetationSurfaceStatus::Failed
					: AshEngine::VegetationSurfaceStatus::Ready;
			const size_t result_count =
				current_mode == BatchMode::Partial && !requests.empty()
					? requests.size() - 1
					: requests.size();
			result.samples.reserve(result_count);
			for (size_t index = 0; index < result_count; ++index)
			{
				const uint32_t request_index =
					current_mode == BatchMode::WrongIndex && index == 0
						? 1u
						: static_cast<uint32_t>(index);
				if (current_mode == BatchMode::Outside ||
					current_mode == BatchMode::Pending ||
					current_mode == BatchMode::Failed)
				{
					result.samples.push_back(
						VegetationTest::NonReadySurfaceSample(
							request_index,
							current_mode == BatchMode::Outside
								? AshEngine::VegetationSurfaceStatus::Outside
							: current_mode == BatchMode::Pending
								? AshEngine::VegetationSurfaceStatus::Pending
								: AshEngine::VegetationSurfaceStatus::Failed));
				}
				else
				{
					result.samples.push_back(VegetationTest::ReadySurfaceSample(
						request_index, 0.0, glm::dvec3(0.0, 1.0, 0.0)));
				}
			}
			return result;
		}

		mutable std::vector<size_t> batch_sizes{};
		mutable std::vector<
			std::vector<AshEngine::VegetationSurfaceSampleRequest>>
			batch_requests{};
		size_t throw_on_identity_call = 0;
		mutable size_t identity_call_count = 0;
		BatchMode mode = BatchMode::Ready;
		size_t fail_on_batch_call = 0;
		mutable size_t batch_call_count = 0;
		std::function<void()> on_batch{};
		std::function<void(const AshEngine::VegetationOperationControl&)>
			on_batch_control{};

	private:
		AshEngine::VegetationSurfaceIdentity m_identity{};
	};

	class SequenceSurfaceProvider final :
		public AshEngine::IVegetationSurfaceProvider
	{
	public:
		AshEngine::VegetationSurfaceCaptureResult capture(
			AshEngine::VegetationSurfaceBinding binding) const override
		{
			++attempt_count;
			last_binding = binding;
			captured_bindings.push_back(binding);
			if (throw_on_capture)
			{
				throw std::runtime_error("capture failure");
			}
			if (!statuses.empty())
			{
				const AshEngine::VegetationSurfaceStatus status = statuses.front();
				statuses.pop_front();
				if (status != AshEngine::VegetationSurfaceStatus::Ready)
				{
					return {
						status,
						attach_snapshot_to_non_ready ? snapshot : nullptr,
						"scripted capture"
					};
				}
			}
			return {
				AshEngine::VegetationSurfaceStatus::Ready,
				snapshot,
				{}
			};
		}

		void PushPending(const size_t count)
		{
			for (size_t index = 0; index < count; ++index)
			{
				statuses.push_back(AshEngine::VegetationSurfaceStatus::Pending);
			}
		}

		mutable std::deque<AshEngine::VegetationSurfaceStatus> statuses{};
		std::shared_ptr<const AshEngine::IVegetationSurfaceSnapshot> snapshot{};
		bool throw_on_capture = false;
		bool attach_snapshot_to_non_ready = false;
		mutable size_t attempt_count = 0;
		mutable AshEngine::VegetationSurfaceBinding last_binding{};
		mutable std::vector<AshEngine::VegetationSurfaceBinding>
			captured_bindings{};
	};

	class RecordingCommandExecutor final :
		public AshEditor::IEditorCommandExecutor
	{
	public:
		bool ExecuteCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			if (!command)
			{
				return false;
			}
			AshEditor::EditorContext context{};
			if (!command->Execute(context))
			{
				return false;
			}
			commands.push_back(std::move(command));
			return true;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			if (!command)
			{
				return AshEditor::EditorCommandRecordResult::RollbackFailed;
			}
			const AshEditor::EditorCommandRecordResult scripted =
				record_results.empty()
					? AshEditor::EditorCommandRecordResult::Recorded
					: record_results.front();
			if (!record_results.empty())
			{
				record_results.pop_front();
			}
			if (scripted == AshEditor::EditorCommandRecordResult::Recorded)
			{
				commands.push_back(std::move(command));
				return scripted;
			}
			if (scripted == AshEditor::EditorCommandRecordResult::RolledBack)
			{
				AshEditor::EditorContext context{};
				return command->Undo(context)
					? AshEditor::EditorCommandRecordResult::RolledBack
					: AshEditor::EditorCommandRecordResult::RollbackFailed;
			}
			return AshEditor::EditorCommandRecordResult::RollbackFailed;
		}

		void ScriptRecordResult(
			const AshEditor::EditorCommandRecordResult result)
		{
			record_results.push_back(result);
		}

		std::size_t RemoveCommandsForDocument(
			const AshEditor::EditorCommandDocumentKey& key) override
		{
			size_t removed = 0;
			for (auto iterator = commands.begin(); iterator != commands.end();)
			{
				const auto document = (*iterator)->GetDocumentKey();
				if (document.has_value() && *document == key)
				{
					iterator = commands.erase(iterator);
					++removed;
				}
				else
				{
					++iterator;
				}
			}
			return removed;
		}

		bool UndoLatest()
		{
			if (commands.empty())
			{
				return false;
			}
			AshEditor::EditorContext context{};
			return commands.back()->Undo(context);
		}

		bool RedoLatest()
		{
			if (commands.empty())
			{
				return false;
			}
			AshEditor::EditorContext context{};
			return commands.back()->Execute(context);
		}

		size_t RecordedCount() const
		{
			return commands.size();
		}

		std::vector<std::unique_ptr<AshEditor::EditorCommand>> commands{};
		std::deque<AshEditor::EditorCommandRecordResult> record_results{};
	};

	class CancellingStageWriter final :
		public AshEngine::IVegetationStageFileWriter
	{
	public:
		CancellingStageWriter(
			std::unique_ptr<AshEngine::IVegetationStageFileWriter> writer,
			std::shared_ptr<std::atomic_bool> cancellation)
			: m_writer(std::move(writer)),
			m_cancellation(std::move(cancellation))
		{
		}

		bool WriteBlock(
			const uint64_t offset,
			const AshEngine::VegetationByteSpan bytes) override
		{
			const bool written = m_writer &&
				m_writer->WriteBlock(offset, bytes);
			if (written && m_cancellation)
			{
				m_cancellation->store(true, std::memory_order_release);
			}
			return written;
		}

		bool FlushAndClose() override
		{
			return m_writer && m_writer->FlushAndClose();
		}

	private:
		std::unique_ptr<AshEngine::IVegetationStageFileWriter> m_writer{};
		std::shared_ptr<std::atomic_bool> m_cancellation{};
	};

	class CancellingFileOps final :
		public AshEngine::IVegetationFileOps
	{
	public:
		explicit CancellingFileOps(AshEngine::IVegetationFileOps& backing)
			: m_backing(backing)
		{
		}

		AshEngine::VegetationFileInspection InspectPath(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& path) override
		{
			++inspect_calls;
			if (throw_on_inspect)
			{
				throw std::runtime_error("scripted InspectPath failure");
			}
			AshEngine::VegetationFileInspection result =
				m_backing.InspectPath(asset_root, path);
			if (cancel_after_inspect && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileBytesResult ReadAllBytes(
			const std::filesystem::path& path,
			const uint64_t max_bytes) override
		{
			++read_calls;
			AshEngine::VegetationFileBytesResult result =
				m_backing.ReadAllBytes(path, max_bytes);
			if (cancel_after_read && cancellation)
			{
				cancellation->store(true, std::memory_order_release);
			}
			return result;
		}

		AshEngine::VegetationFileResultStatus EnsureDirectoryTree(
			const std::filesystem::path& asset_root,
			const std::filesystem::path& relative_directory) override
		{
			return m_backing.EnsureDirectoryTree(
				asset_root, relative_directory);
		}

		AshEngine::VegetationStageFileResult CreateUniqueSiblingStageFile(
			const std::filesystem::path& target,
			const uint64_t operation_serial) override
		{
			created_stage_serials.push_back(operation_serial);
			return Wrap(m_backing.CreateUniqueSiblingStageFile(
				target, operation_serial));
		}

		AshEngine::VegetationStageTreeResult CreateUniqueStageTree(
			const std::filesystem::path& store_root,
			const uint64_t operation_serial) override
		{
			created_tree_serials.push_back(operation_serial);
			return m_backing.CreateUniqueStageTree(
				store_root, operation_serial);
		}

		AshEngine::VegetationStageFileResult CreateOwnedStageFile(
			const std::filesystem::path& owned_stage_root,
			const std::filesystem::path& relative_path) override
		{
			return Wrap(m_backing.CreateOwnedStageFile(
				owned_stage_root, relative_path));
		}

		bool RemoveOwnedStageFile(
			const std::filesystem::path& stage_file,
			const AshEngine::VegetationFileIdentity& expected_identity) override
		{
			++remove_file_calls;
			if (!allow_remove)
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
			++remove_tree_calls;
			if (!allow_remove)
			{
				return false;
			}
			return m_backing.RemoveOwnedStageTree(
				stage_root, expected_identity);
		}

		AshEngine::VegetationCreateNewStatus PublishImmutableFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& content_addressed_target) override
		{
			return m_backing.PublishImmutableFromStage(
				stage, content_addressed_target);
		}

		AshEngine::VegetationFileLeaseResult AcquireNamedLease(
			const std::string_view canonical_identity,
			const AshEngine::VegetationOperationControl& control) override
		{
			return m_backing.AcquireNamedLease(canonical_identity, control);
		}

		AshEngine::VegetationAtomicReplaceResult AtomicReplace(
			const std::filesystem::path& stage,
			const std::filesystem::path& target,
			AshEngine::VegetationOwnedStageCleanupRegistry&
				cleanup_registry) override
		{
			return m_backing.AtomicReplace(stage, target, cleanup_registry);
		}

		AshEngine::VegetationCreateNewStatus CreateNewFromStage(
			const std::filesystem::path& stage,
			const std::filesystem::path& target) override
		{
			return m_backing.CreateNewFromStage(stage, target);
		}

		std::shared_ptr<std::atomic_bool> cancellation{};
		bool cancel_after_inspect = false;
		bool cancel_after_read = false;
		bool throw_on_inspect = false;
		bool allow_remove = true;
		size_t inspect_calls = 0;
		size_t read_calls = 0;
		size_t remove_file_calls = 0;
		size_t remove_tree_calls = 0;
		std::vector<uint64_t> created_stage_serials{};
		std::vector<uint64_t> created_tree_serials{};

	private:
		AshEngine::VegetationStageFileResult Wrap(
			AshEngine::VegetationStageFileResult result)
		{
			if (result.status ==
					AshEngine::VegetationFileResultStatus::Succeeded &&
				result.writer)
			{
				result.writer = std::make_unique<CancellingStageWriter>(
					std::move(result.writer), cancellation);
			}
			return result;
		}

		AshEngine::IVegetationFileOps& m_backing;
	};

	struct ServiceFixture
	{
		explicit ServiceFixture(
			const std::string& label,
			const bool write_layer = true,
			const AshEngine::IVegetationSurfaceProvider* provider = nullptr)
			: root(label)
		{
			root.Write(
				"vegetation/Phase2ManualSpecies.AshVegetation",
				VegetationTest::CanonicalGrassSpeciesJson());
			root.Write(
				"vegetation/Phase2ManualSpeciesReplacement.AshVegetation",
				VegetationTest::ReadFixtureBytes(
					"project/src/tests/fixtures/vegetation/"
					"Phase2ManualSpeciesReplacement.AshVegetation"));
			root.Write(
				"vegetation/SecondarySpecies.AshVegetation",
				VegetationTest::ReplaceJsonToken(
					VegetationTest::CanonicalGrassSpeciesJson(),
					"00112233445566778899aabbccddeeff",
					"10112233445566778899aabbccddeeff"));
			if (write_layer)
			{
				root.Write(
					"vegetation/test.AshVegetationLayer",
					VegetationTest::ResolvedMinimalLayerBytes());
			}
			database = AshEngine::AssetDatabase::create(root.Path());
			deps.pAssetDatabase = &database;
			deps.asset_root = root.Path();
			deps.pCommandExecutor = &commands;
			deps.pSurfaceProvider = provider;
			deps.pTaskExecutor = &executor;
			deps.load_budget = VegetationTest::GenerousLoadBudget();
			deps.chunk_set_load_budget =
				AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
			deps.create_layer_id = []
			{
				return VegetationTest::SequentialId(101);
			};
		}

		VegetationTest::ScopedAssetRoot root;
		AshEngine::AssetDatabase database{};
		VegetationTest::ManualVegetationEditorTaskExecutor executor{};
		RecordingCommandExecutor commands{};
		AshEditor::VegetationEditorServiceDeps deps{};
	};

	AshEngine::VegetationBrushStroke ValidStroke(
		const AshEngine::VegetationBrushMode mode =
			AshEngine::VegetationBrushMode::Paint)
	{
		AshEngine::VegetationBrushStroke stroke{};
		stroke.mode = mode;
		stroke.selected_species =
			VegetationTest::ResolvedMinimalPaletteEntry().species_id;
		stroke.radius_mm = 1000;
		stroke.spacing_mm = 1;
		stroke.strength = 1;
		return stroke;
	}

	std::vector<uint8_t> EncodeLayerOrThrow(
		const AshEngine::VegetationLayerSnapshot& snapshot)
	{
		std::vector<uint8_t> bytes{};
		std::string error{};
		if (!AshEngine::encode_vegetation_layer(snapshot, bytes, &error))
		{
			throw std::runtime_error(
				"Vegetation service test Layer did not encode: " + error);
		}
		return bytes;
	}

	AshEngine::VegetationLayerSnapshot DecodeLayerOrThrow(
		const std::vector<uint8_t>& bytes)
	{
		AshEngine::VegetationLayerSnapshot snapshot{};
		std::string error{};
		if (!AshEngine::decode_vegetation_layer(
				bytes,
				VegetationTest::GenerousLoadBudget(),
				snapshot,
				&error))
		{
			throw std::runtime_error(
				"Vegetation service test Layer did not decode: " + error);
		}
		return snapshot;
	}

	std::vector<uint8_t> AuthoringBytesIgnoringGeneration(
		AshEngine::VegetationLayerSnapshot snapshot)
	{
		snapshot.content_generation = 1;
		return EncodeLayerOrThrow(snapshot);
	}

	std::string VegetationIdHex(const AshEngine::VegetationId& id)
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string hex{};
		hex.reserve(id.size() * 2);
		for (const uint8_t byte : id)
		{
			hex.push_back(digits[byte >> 4u]);
			hex.push_back(digits[byte & 0x0fu]);
		}
		return hex;
	}

	bool HasOwnedOperationStage(const std::filesystem::path& root)
	{
		std::error_code error{};
		for (std::filesystem::recursive_directory_iterator iterator(
				root,
				std::filesystem::directory_options::skip_permission_denied,
				error),
			end;
			iterator != end && !error;
			iterator.increment(error))
		{
			const std::wstring name =
				iterator->path().filename().wstring();
			if (name.rfind(L".ashveg-layer-stage-", 0) == 0 ||
				name.rfind(L".ashveg-stage-tree-", 0) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("Vegetation executor executes submitted work")
{
	AshEditor::VegetationEditorTaskExecutor executor{};
	std::atomic_uint32_t runs{ 0 };
	const uint64_t task = executor.Submit({
		std::chrono::steady_clock::now() + 30s,
		[&](AshEngine::VegetationOperationControl)
		{
			runs.fetch_add(1, std::memory_order_acq_rel);
		}
	});
	while (!executor.IsComplete(task))
	{
		std::this_thread::yield();
	}
	CHECK(runs.load(std::memory_order_acquire) == 1);
	CHECK(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK(executor.IsIdle());
	executor.Join(task);
	CHECK_FALSE(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK_NOTHROW(executor.Join(task));
	CHECK(executor.IsIdle());
}

TEST_CASE("Vegetation executor cancellation wins before semantic work")
{
	AshEditor::VegetationEditorTaskExecutor executor{};
	std::atomic_bool observed_cancel{ false };
	std::atomic_uint32_t semantic_side_effects{ 0 };
	Barrier caller_barrier{};

	const auto deadline = std::chrono::steady_clock::now() + 30s;
	const uint64_t task = executor.Submit({
		deadline,
		[&](const AshEngine::VegetationOperationControl control)
		{
			caller_barrier.Arrive();
			caller_barrier.WaitForRelease();
			const bool cancelled =
				control.cancel_requested &&
				control.cancel_requested->load(std::memory_order_acquire);
			observed_cancel.store(cancelled, std::memory_order_release);
			if (cancelled)
			{
				return;
			}
			semantic_side_effects.fetch_add(1, std::memory_order_acq_rel);
		}
	});
	caller_barrier.WaitForArrival();
	executor.RequestCancel(task);
	caller_barrier.Release();
	while (!executor.IsComplete(task))
	{
		std::this_thread::yield();
	}

	CHECK(observed_cancel.load(std::memory_order_acquire));
	CHECK(semantic_side_effects.load(std::memory_order_acquire) == 0);
	CHECK(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK(executor.IsIdle());
	executor.Join(task);
	CHECK_FALSE(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK_NOTHROW(executor.Join(task));
	CHECK(executor.IsIdle());
}

TEST_CASE("Vegetation executor manual seam skips body after cancel before run")
{
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	uint32_t body_calls = 0;
	const uint64_t task = executor.Submit({
		std::chrono::steady_clock::now() + 30s,
		[&](AshEngine::VegetationOperationControl)
		{
			++body_calls;
		}
	});
	REQUIRE(task != 0);
	executor.RequestCancel(task);
	CHECK(executor.IsComplete(task));
	executor.RunAll();

	CHECK(body_calls == 0);
	CHECK(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK(executor.IsIdle());
	executor.Join(task);
	CHECK(executor.JoinedCount() == 1);
	CHECK_FALSE(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK(executor.TaskRecordCount() == 0);
	CHECK(executor.ScheduledOrderCount() == 0);
	CHECK_NOTHROW(executor.Join(task));
	CHECK(executor.JoinedCount() == 1);
}

TEST_CASE("Vegetation executor manual seam consumes observed exception at Join")
{
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	const uint64_t task = executor.Submit({
		std::chrono::steady_clock::now() + 30s,
		[](AshEngine::VegetationOperationControl)
		{
			throw std::runtime_error("manual executor test failure");
		}
	});
	REQUIRE(task != 0);
	executor.RunAll();

	CHECK(executor.IsComplete(task));
	CHECK(executor.GetException(task) != nullptr);
	executor.Join(task);
	CHECK_FALSE(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK(executor.TaskRecordCount() == 0);
	CHECK(executor.ScheduledOrderCount() == 0);
	CHECK(executor.JoinedCount() == 1);
	CHECK_NOTHROW(executor.Join(task));
	CHECK(executor.JoinedCount() == 1);
}

TEST_CASE("Vegetation executor manual seam cancel all consumes every task")
{
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	uint32_t body_calls = 0;
	const auto submit = [&]()
	{
		return executor.Submit({
			std::chrono::steady_clock::now() + 30s,
			[&](AshEngine::VegetationOperationControl)
			{
				++body_calls;
			}
		});
	};
	const uint64_t first_task = submit();
	const uint64_t second_task = submit();
	REQUIRE(first_task != 0);
	REQUIRE(second_task != 0);

	executor.CancelAndJoinAll();

	CHECK(body_calls == 0);
	CHECK_FALSE(executor.IsComplete(first_task));
	CHECK_FALSE(executor.IsComplete(second_task));
	CHECK(executor.GetException(first_task) == nullptr);
	CHECK(executor.GetException(second_task) == nullptr);
	CHECK(executor.TaskRecordCount() == 0);
	CHECK(executor.ScheduledOrderCount() == 0);
	CHECK(executor.JoinedCount() == 2);
	CHECK_NOTHROW(executor.CancelAndJoinAll());
	CHECK(executor.JoinedCount() == 2);
	CHECK(executor.IsIdle());
}

TEST_CASE("Vegetation executor manual seam releases sequential task history")
{
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	for (size_t index = 0; index < 256; ++index)
	{
		const uint64_t task = executor.Submit({
			std::chrono::steady_clock::now() + 30s,
			[](AshEngine::VegetationOperationControl)
			{
			}
		});
		REQUIRE(task != 0);
		REQUIRE(executor.RunNext());
		CHECK(executor.IsComplete(task));
		CHECK(executor.GetException(task) == nullptr);
		executor.Join(task);
		CHECK_FALSE(executor.IsComplete(task));
		CHECK(executor.GetException(task) == nullptr);
		CHECK(executor.TaskRecordCount() == 0);
		CHECK(executor.ScheduledOrderCount() == 0);
	}
	CHECK(executor.JoinedCount() == 256);
}

TEST_CASE("Vegetation executor records exceptions and joins each task exactly once")
{
	AshEditor::VegetationEditorTaskExecutor executor{};
	const uint64_t task = executor.Submit({
		std::chrono::steady_clock::now() + 30s,
		[](AshEngine::VegetationOperationControl)
		{
			throw std::runtime_error("executor test failure");
		}
	});

	while (!executor.IsComplete(task))
	{
		std::this_thread::yield();
	}
	CHECK(executor.IsComplete(task));
	CHECK(executor.GetException(task) != nullptr);
	CHECK(executor.IsIdle());
	executor.Join(task);
	CHECK_FALSE(executor.IsComplete(task));
	CHECK(executor.GetException(task) == nullptr);
	CHECK_NOTHROW(executor.Join(task));
	CHECK(executor.IsIdle());
}

TEST_CASE("Vegetation executor cancel all acknowledges and joins every task")
{
	AshEditor::VegetationEditorTaskExecutor executor{};
	Barrier first{};
	Barrier second{};
	std::atomic_uint32_t cancelled{ 0 };
	const auto submit = [&](Barrier& barrier)
	{
		return executor.Submit({
			std::chrono::steady_clock::now() + 30s,
			[&barrier, &cancelled](const AshEngine::VegetationOperationControl control)
			{
				barrier.Arrive();
				while (control.cancel_requested &&
					!control.cancel_requested->load(std::memory_order_acquire))
				{
					std::this_thread::yield();
				}
				cancelled.fetch_add(1, std::memory_order_acq_rel);
			}
		});
	};

	const uint64_t first_task = submit(first);
	first.WaitForArrival();
	const uint64_t second_task = submit(second);
	second.WaitForArrival();

	executor.CancelAndJoinAll();

	CHECK(cancelled.load(std::memory_order_acquire) == 2);
	CHECK(executor.IsIdle());
	CHECK_FALSE(executor.IsComplete(first_task));
	CHECK_FALSE(executor.IsComplete(second_task));
	CHECK(executor.GetException(first_task) == nullptr);
	CHECK(executor.GetException(second_task) == nullptr);
	CHECK_NOTHROW(executor.Join(first_task));
	CHECK_NOTHROW(executor.Join(second_task));
}

TEST_CASE("Vegetation executor consumes completed task records at Join")
{
	AshEditor::VegetationEditorTaskExecutor executor{};
	for (size_t index = 0; index < 256; ++index)
	{
		const uint64_t task = executor.Submit({
			std::chrono::steady_clock::now() + 30s,
			[](AshEngine::VegetationOperationControl)
			{
			}
		});
		REQUIRE(task != 0);
		while (!executor.IsComplete(task))
		{
			std::this_thread::yield();
		}
		CHECK(executor.GetException(task) == nullptr);
		executor.Join(task);
		CHECK_FALSE(executor.IsComplete(task));
		CHECK(executor.GetException(task) == nullptr);
		CHECK(executor.IsIdle());
	}
}

TEST_CASE("Vegetation service default resident budgets are exact")
{
	const auto check_load_defaults =
		[](const AshEngine::VegetationLoadBudget& load)
		{
			CHECK(load.max_file_bytes == 256ull * 1024ull * 1024ull);
			CHECK(load.max_payload_bytes == 256ull * 1024ull * 1024ull);
			CHECK(load.max_decoded_bytes == 1024ull * 1024ull * 1024ull);
			CHECK(load.max_palette_records == 65534u);
			CHECK(load.max_tile_records == 262144u);
			CHECK(load.max_instance_records == 8388608u);
		};
	const auto check_chunk_defaults =
		[&check_load_defaults](
			const AshEngine::VegetationChunkSetLoadBudget& chunk_set)
		{
			check_load_defaults(chunk_set.per_file);
			CHECK(chunk_set.max_manifest_entries == 262144u);
			CHECK(chunk_set.max_total_inspected_bytes ==
				2ull * 1024ull * 1024ull * 1024ull);
			CHECK(chunk_set.max_summary_bytes == 64ull * 1024ull * 1024ull);
		};

	AshEngine::VegetationLoadBudget mutable_load =
		AshEditor::VegetationEditorService::DefaultLoadBudget();
	const AshEngine::VegetationLoadBudget independent_load =
		AshEditor::VegetationEditorService::DefaultLoadBudget();
	mutable_load.max_file_bytes = 1;
	mutable_load.max_payload_bytes = 2;
	mutable_load.max_decoded_bytes = 3;
	mutable_load.max_palette_records = 4;
	mutable_load.max_tile_records = 5;
	mutable_load.max_instance_records = 6;
	check_load_defaults(independent_load);
	check_load_defaults(
		AshEditor::VegetationEditorService::DefaultLoadBudget());

	AshEngine::VegetationChunkSetLoadBudget mutable_chunk =
		AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
	const AshEngine::VegetationChunkSetLoadBudget independent_chunk =
		AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
	mutable_chunk.per_file.max_file_bytes = 11;
	mutable_chunk.per_file.max_payload_bytes = 12;
	mutable_chunk.per_file.max_decoded_bytes = 13;
	mutable_chunk.per_file.max_palette_records = 14;
	mutable_chunk.per_file.max_tile_records = 15;
	mutable_chunk.per_file.max_instance_records = 16;
	mutable_chunk.max_manifest_entries = 17;
	mutable_chunk.max_total_inspected_bytes = 18;
	mutable_chunk.max_summary_bytes = 19;
	check_chunk_defaults(independent_chunk);
	check_chunk_defaults(
		AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget());
}

TEST_CASE("Vegetation service rejects a zero bake binding without side effects")
{
	for (const bool with_provider : { false, true })
	{
		SequenceSurfaceProvider provider{};
		provider.PushPending(1);
		ServiceFixture fixture(
			"service-zero-bake-binding-" + std::to_string(with_provider),
			true,
			with_provider ? &provider : nullptr);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));

		const auto before_state = service.GetOperationState();
		const uint64_t before_generation = service.GetContentGeneration();
		const auto before_capabilities = service.GetCapabilities();
		const auto now = std::chrono::steady_clock::now();
		CHECK_FALSE(service.RequestBake(
			AshEngine::VegetationSurfaceBinding{}, now));
		CHECK(provider.attempt_count == 0);
		CHECK(provider.captured_bindings.empty());
		CHECK(fixture.executor.IsIdle());
		CHECK(service.GetOperationState() == before_state);
		CHECK(service.GetContentGeneration() == before_generation);
		CHECK(service.GetCapabilities().can_bake ==
			before_capabilities.can_bake);
	}
}

TEST_CASE("Vegetation service rejects a captured zero surface identity before submission")
{
	SequenceSurfaceProvider provider{};
	provider.snapshot =
		std::make_shared<ReadySurfaceSnapshot>(
			AshEngine::VegetationSurfaceIdentity{});
	ServiceFixture fixture(
		"service-zero-captured-surface-identity", true, &provider);
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const auto now = std::chrono::steady_clock::now();

	CHECK_FALSE(service.RequestBake({ 42 }, now));

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK(provider.attempt_count == 1);
	REQUIRE(provider.captured_bindings.size() == 1);
	CHECK(provider.captured_bindings.front().surface_entity_id == 42);
	CHECK(fixture.executor.IsIdle());
	CHECK(fixture.executor.PendingCount() == 0);
	CHECK(file_ops.created_tree_serials.empty());
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	CHECK_FALSE(std::filesystem::exists(
		fixture.root.Path() /
		"vegetation/test.AshVegetationLayer.AshVegetationChunks/active.asva"));

	provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(67, 1, 2, 3));
	REQUIRE(service.RequestBake({ 777 }, now + 1ms));
	REQUIRE(provider.captured_bindings.size() == 2);
	CHECK(provider.captured_bindings.back().surface_entity_id == 777);
	fixture.executor.RunAll();
	service.Tick(now + 1ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
}

TEST_CASE("Vegetation service releases a large frozen palette before the next bake request")
{
	SequenceSurfaceProvider provider{};
	provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(153, 1, 2, 3));
	provider.PushPending(1);
	provider.statuses.push_back(
		AshEngine::VegetationSurfaceStatus::Failed);
	ServiceFixture fixture(
		"service-large-palette-request-lifetime", true, &provider);

	AshEngine::VegetationLayerSnapshot large_layer =
		VegetationTest::ResolvedMinimalLayerSnapshot();
	for (uint8_t index = 0; index < 24; ++index)
	{
		const AshEngine::VegetationId species_id =
			VegetationTest::SequentialId(static_cast<uint8_t>(64 + index));
		const std::vector<uint8_t> source = VegetationTest::ReplaceJsonToken(
			VegetationTest::CanonicalGrassSpeciesJson(),
			"00112233445566778899aabbccddeeff",
			VegetationIdHex(species_id));
		AshEngine::VegetationSpecies species{};
		std::string error{};
		REQUIRE(AshEngine::decode_vegetation_species(
			source, VegetationTest::GenerousLoadBudget(), species, &error));
		std::vector<uint8_t> canonical{};
		REQUIRE(AshEngine::encode_vegetation_species(
			species, canonical, &error));
		const std::filesystem::path relative_path =
			"vegetation/LargeSpecies" + std::to_string(index) +
			".AshVegetation";
		fixture.root.Write(relative_path, canonical);
		large_layer.palette.push_back({
			species.species_id,
			AshEngine::vegetation_sha256(
				canonical.data(), canonical.size()),
			relative_path.generic_u8string()
		});
	}
	fixture.root.Write(
		"vegetation/test.AshVegetationLayer",
		EncodeLayerOrThrow(large_layer));
	REQUIRE(fixture.database.refresh());

	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.GetPaletteView()->size() == large_layer.palette.size());
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestBake({ 42 }, now));
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Pending);

	service.Tick(now + 50ms);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK(fixture.executor.TaskRecordCount() == 0);
	fixture.root.Write(
		"vegetation/test.AshVegetationLayer",
		VegetationTest::ResolvedMinimalLayerBytes());
	REQUIRE(service.RequestReload(now + 51ms));
	fixture.executor.RunAll();
	service.Tick(now + 51ms);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	REQUIRE(service.GetPaletteView()->size() == 1);

	REQUIRE(service.RequestBake({ 777 }, now + 52ms));
	fixture.executor.RunAll();
	service.Tick(now + 52ms);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	REQUIRE(provider.captured_bindings.size() == 4);
	CHECK(provider.captured_bindings[0].surface_entity_id == 42);
	CHECK(provider.captured_bindings[1].surface_entity_id == 42);
	CHECK(provider.captured_bindings[2].surface_entity_id == 777);
	CHECK(provider.captured_bindings[3].surface_entity_id == 777);
	CHECK(fixture.executor.TaskRecordCount() == 0);
	CHECK(fixture.executor.ScheduledOrderCount() == 0);
}

TEST_CASE("Vegetation service checks operation conflicts before bake capability")
{
	ServiceFixture fixture("service-conflict-before-bake-capability");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Running);
	REQUIRE(fixture.executor.PendingCount() == 1);

	CHECK_FALSE(service.RequestBake({ 42 }, now));
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Running);
	CHECK(fixture.executor.PendingCount() == 1);
	CHECK_FALSE(service.RequestBake({}, now));
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Running);
	CHECK(fixture.executor.PendingCount() == 1);
}

TEST_CASE("Vegetation service consumes a serial only after task submission succeeds")
{
	{
		ServiceFixture fixture("service-save-submit-serial");
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const auto now = std::chrono::steady_clock::now();
		fixture.executor.RejectNextSubmission();
		CHECK_FALSE(service.RequestSave(now));
		REQUIRE(service.RequestSave(now + 1ms));
		fixture.executor.RunAll();
		service.Tick(now + 1ms);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		REQUIRE(file_ops.created_stage_serials.size() == 1);
		CHECK(file_ops.created_stage_serials.front() == 1);
	}

	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(88, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-submit-serial", true, &provider);
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		fixture.executor.RejectNextSubmission();
		CHECK_FALSE(service.RequestBake({ 42 }, now));
		REQUIRE(service.RequestBake({ 42 }, now + 1ms));
		fixture.executor.RunAll();
		service.Tick(now + 1ms);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		REQUIRE(file_ops.created_tree_serials.size() == 1);
		CHECK(file_ops.created_tree_serials.front() == 1);
	}
}

TEST_CASE("Vegetation service retries Pending with the exact bounded schedule")
{
	SequenceSurfaceProvider provider{};
	provider.PushPending(9);
	ServiceFixture fixture("service-pending", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));

	AshEngine::VegetationSurfaceBinding binding{ 42 };
	const auto t0 = std::chrono::steady_clock::now();
	REQUIRE(service.RequestBake(binding, t0));
	CHECK(provider.attempt_count == 1);
	REQUIRE(provider.captured_bindings.size() == 1);
	CHECK(provider.captured_bindings.front().surface_entity_id == 42);
	CHECK(fixture.executor.IsIdle());
	binding.surface_entity_id = 999;
	const std::array<std::chrono::milliseconds, 8> schedule{
		50ms, 100ms, 200ms, 400ms, 800ms, 1000ms, 1000ms, 1000ms
	};
	std::chrono::milliseconds elapsed{ 0 };
	for (size_t index = 0; index < schedule.size(); ++index)
	{
		service.Tick(t0 + elapsed + schedule[index] - 1ms);
		CHECK(provider.attempt_count == index + 1);
		elapsed += schedule[index];
		service.Tick(t0 + elapsed);
		CHECK(provider.attempt_count == index + 2);
		REQUIRE(provider.captured_bindings.size() == index + 2);
		CHECK(provider.captured_bindings.back().surface_entity_id == 42);
		CHECK(fixture.executor.IsIdle());
	}
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::TimedOut);
	CHECK(fixture.commands.RecordedCount() == 0);
	provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(121, 1, 2, 3));
	REQUIRE(service.RequestBake({ 777 }, t0 + elapsed + 1ms));
	REQUIRE(provider.captured_bindings.size() == 10);
	CHECK(provider.captured_bindings.back().surface_entity_id == 777);
	CHECK(fixture.executor.PendingCount() == 1);
}

TEST_CASE("Vegetation service schedules only after Pending becomes Ready")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(44, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	provider.PushPending(1);
	ServiceFixture fixture("service-pending-ready", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));

	const auto t0 = std::chrono::steady_clock::now();
	REQUIRE(service.RequestBake({ 42 }, t0));
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Pending);
	CHECK(fixture.executor.IsIdle());
	service.Tick(t0 + 49ms);
	CHECK(provider.attempt_count == 1);
	service.Tick(t0 + 50ms);
	CHECK(provider.attempt_count == 2);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Running);
	CHECK(fixture.executor.PendingCount() == 1);
	service.Shutdown();
	CHECK(fixture.executor.IsIdle());
	CHECK(fixture.executor.JoinedCount() == 1);
}

TEST_CASE("Vegetation service Pending bake preserves its original request identity")
{
	SUBCASE("undo and redo while Pending cannot be absorbed")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(45, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		provider.PushPending(1);
		ServiceFixture fixture(
			"service-pending-working-set-race", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const auto t0 = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, t0));
		REQUIRE(fixture.commands.UndoLatest());
		REQUIRE(fixture.commands.RedoLatest());
		const uint64_t generation = service.GetContentGeneration();

		service.Tick(t0 + 50ms);
		REQUIRE(fixture.executor.PendingCount() == 1);
		fixture.executor.RunAll();
		service.Tick(t0 + 50ms);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(service.GetContentGeneration() == generation);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}

	SUBCASE("external Layer change while Pending cannot be absorbed")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(46, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		provider.PushPending(1);
		ServiceFixture fixture(
			"service-pending-source-race", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto t0 = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, t0));
		fixture.root.Write(
			"vegetation/test.AshVegetationLayer",
			VegetationTest::DifferentValidLayerBytes());

		service.Tick(t0 + 50ms);
		REQUIRE(fixture.executor.PendingCount() == 1);
		fixture.executor.RunAll();
		service.Tick(t0 + 50ms);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}
}

TEST_CASE("Vegetation service fails closed for invalid bake captures")
{
	enum class Scenario : uint8_t
	{
		Failed = 0,
		Outside,
		ReadyNull,
		PendingWithSnapshot,
		Throw
	};
	const std::array scenarios{
		Scenario::Failed,
		Scenario::Outside,
		Scenario::ReadyNull,
		Scenario::PendingWithSnapshot,
		Scenario::Throw
	};
	for (size_t index = 0; index < scenarios.size(); ++index)
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(
				static_cast<uint8_t>(50 + index), 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = scenarios[index] == Scenario::ReadyNull
			? nullptr
			: snapshot;
		provider.throw_on_capture = scenarios[index] == Scenario::Throw;
		provider.attach_snapshot_to_non_ready =
			scenarios[index] == Scenario::PendingWithSnapshot;
		if (scenarios[index] != Scenario::ReadyNull &&
			scenarios[index] != Scenario::Throw)
		{
			provider.statuses.push_back(
				scenarios[index] == Scenario::Failed
					? AshEngine::VegetationSurfaceStatus::Failed
				: scenarios[index] == Scenario::Outside
					? AshEngine::VegetationSurfaceStatus::Outside
					: AshEngine::VegetationSurfaceStatus::Pending);
		}
		ServiceFixture fixture(
			"service-invalid-bake-capture-" + std::to_string(index),
			true,
			&provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		CHECK_FALSE(service.RequestBake(
			{ static_cast<uint64_t>(60 + index) },
			std::chrono::steady_clock::now()));
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(fixture.executor.IsIdle());
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
	}
}

TEST_CASE("Vegetation service contains every surface identity exception")
{
	SUBCASE("BeginStroke")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(47, 1, 2, 3));
		snapshot->throw_on_identity_call = 1;
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-stroke-identity-throw", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		bool began = true;
		CHECK_NOTHROW(began = service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(began);
		CHECK(fixture.executor.IsIdle());
	}

	SUBCASE("stroke completion recapture")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(48, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-stroke-completion-identity-throw", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		REQUIRE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.EndStroke(now));
		fixture.executor.RunAll();
		const auto throwing_snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(48, 1, 2, 3));
		throwing_snapshot->throw_on_identity_call = 1;
		provider.snapshot = throwing_snapshot;
		CHECK_NOTHROW(service.Tick(now));
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(fixture.commands.RecordedCount() == 0);
	}

	SUBCASE("bake request and completion recapture")
	{
		for (const bool throw_during_request : { true, false })
		{
			const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
				VegetationTest::SurfaceIdentity(
					static_cast<uint8_t>(49 + !throw_during_request), 1, 2, 3));
			snapshot->throw_on_identity_call =
				throw_during_request ? 1 : 0;
			SequenceSurfaceProvider provider{};
			provider.snapshot = snapshot;
			ServiceFixture fixture(
				"service-bake-identity-throw-" +
					std::to_string(throw_during_request),
				true,
				&provider);
			AshEditor::VegetationEditorService service(std::move(fixture.deps));
			REQUIRE(service.Initialize());
			REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
			const auto now = std::chrono::steady_clock::now();
			bool requested = false;
			CHECK_NOTHROW(requested = service.RequestBake({ 42 }, now));
			if (throw_during_request)
			{
				CHECK_FALSE(requested);
				CHECK(fixture.executor.IsIdle());
			}
			else
			{
				REQUIRE(requested);
				fixture.executor.RunAll();
				const auto throwing_snapshot =
					std::make_shared<ReadySurfaceSnapshot>(
						VegetationTest::SurfaceIdentity(50, 1, 2, 3));
				throwing_snapshot->throw_on_identity_call = 1;
				provider.snapshot = throwing_snapshot;
				CHECK_NOTHROW(service.Tick(now));
				CHECK(service.GetOperationState() ==
					AshEditor::VegetationOperationState::SourceChanged);
				CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
			}
		}
	}
}

TEST_CASE("Vegetation service without a provider keeps palette and persistence")
{
	ServiceFixture fixture("service-no-provider", false);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.CreateLayer(
		"vegetation/new.AshVegetationLayer", 0x1234u));
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/Phase2ManualSpecies.AshVegetation"));
	REQUIRE(fixture.commands.RecordedCount() == 1);

	const auto palette = service.GetPaletteView();
	REQUIRE(static_cast<bool>(palette));
	REQUIRE(palette->size() == 1);
	CHECK(palette->front().species_id ==
		VegetationTest::ResolvedMinimalPaletteEntry().species_id);
	const auto capabilities = service.GetCapabilities();
	CHECK(capabilities.can_load);
	CHECK(capabilities.can_save);
	CHECK_FALSE(capabilities.can_paint);
	CHECK_FALSE(capabilities.can_bake);
	CHECK(capabilities.surface_unavailable_reason ==
		"No vegetation surface provider is registered.");

	AshEngine::VegetationBrushStroke stroke{};
	const uint64_t before_generation = service.GetContentGeneration();
	CHECK_FALSE(service.BeginStroke(stroke, {}));
	CHECK_FALSE(service.RequestBake(
		AshEngine::VegetationSurfaceBinding{ 42 },
		std::chrono::steady_clock::now()));
	CHECK(service.GetContentGeneration() == before_generation);

	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	REQUIRE(fixture.executor.PendingCount() == 1);
	fixture.executor.RunAll();
	CHECK_FALSE(std::filesystem::exists(
		fixture.root.Path() / "vegetation/new.AshVegetationLayer"));
	service.Tick(now);
	REQUIRE(std::filesystem::is_regular_file(
		fixture.root.Path() / "vegetation/new.AshVegetationLayer"));
	REQUIRE(service.RequestReload(now + 1ms));
	fixture.executor.RunAll();
	service.Tick(now + 1ms);
	REQUIRE(service.GetPaletteView()->size() == 1);
	CHECK(service.GetPaletteView()->front().species_digest ==
		palette->front().species_digest);
}

TEST_CASE("Vegetation service first save preserves a racing creator")
{
	ServiceFixture fixture("service-first-save-race", false);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.CreateLayer(
		"vegetation/race.AshVegetationLayer", 9));
	const uint64_t generation = service.GetContentGeneration();
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));

	fixture.executor.RunAll();
	const std::vector<uint8_t> foreign =
		VegetationTest::ResolvedMinimalLayerBytes();
	fixture.root.Write("vegetation/race.AshVegetationLayer", foreign);
	service.Tick(now);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::AlreadyExists);
	CHECK(VegetationTest::ReadAllBytes(
		fixture.root.Path() / "vegetation/race.AshVegetationLayer") == foreign);
	CHECK(service.GetContentGeneration() == generation);
	CHECK(service.GetCapabilities().can_save);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
}

TEST_CASE("Vegetation service rejects dirty reload without side effects")
{
	ServiceFixture fixture("service-dirty-reload");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const uint64_t before = service.GetContentGeneration();
	REQUIRE(service.RemovePaletteSpecies(
		VegetationTest::ResolvedMinimalPaletteEntry().species_id, true));
	const uint64_t dirty = service.GetContentGeneration();
	REQUIRE(dirty == before + 1);
	CHECK_FALSE(service.RequestReload(std::chrono::steady_clock::now()));
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::DirtyConflict);
	CHECK(service.GetContentGeneration() == dirty);
	CHECK(service.GetPaletteView()->empty());
}

TEST_CASE("Vegetation service palette commands keep immutable views synchronized")
{
	ServiceFixture fixture("service-palette-commands");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const AshEngine::VegetationId original_id =
		VegetationTest::ResolvedMinimalPaletteEntry().species_id;
	const auto now = std::chrono::steady_clock::now();

	const uint64_t opened_generation = service.GetContentGeneration();
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	REQUIRE(service.GetPaletteView()->size() == 2);
	const uint64_t added_generation = service.GetContentGeneration();
	CHECK(added_generation == opened_generation + 1);
	CHECK(fixture.commands.RecordedCount() == 1);

	REQUIRE(fixture.commands.UndoLatest());
	CHECK(service.GetContentGeneration() == added_generation + 1);
	REQUIRE(service.GetPaletteView()->size() == 1);
	CHECK(service.GetPaletteView()->front().species_id == original_id);

	REQUIRE(fixture.commands.RedoLatest());
	service.Tick(now + 1ms);
	CHECK(service.GetContentGeneration() == added_generation + 2);
	REQUIRE(service.GetPaletteView()->size() == 2);

	const auto original = std::find_if(
		service.GetPaletteView()->begin(), service.GetPaletteView()->end(),
		[&original_id](const AshEditor::VegetationPaletteViewEntry& entry)
		{
			return entry.species_id == original_id;
		});
	REQUIRE(original != service.GetPaletteView()->end());
	const AshEngine::VegetationSha256 original_digest =
		original->species_digest;
	REQUIRE(service.ReplacePaletteSpecies(
		original_id,
		"vegetation/Phase2ManualSpeciesReplacement.AshVegetation"));
	REQUIRE(service.GetPaletteView()->size() == 2);
	const auto replaced = std::find_if(
		service.GetPaletteView()->begin(), service.GetPaletteView()->end(),
		[&original_id](const AshEditor::VegetationPaletteViewEntry& entry)
		{
			return entry.species_id == original_id;
		});
	REQUIRE(replaced != service.GetPaletteView()->end());
	CHECK(replaced->species_digest != original_digest);
	CHECK(fixture.commands.RecordedCount() == 2);

	REQUIRE(fixture.commands.UndoLatest());
	service.Tick(now + 2ms);
	const auto restored = std::find_if(
		service.GetPaletteView()->begin(), service.GetPaletteView()->end(),
		[&original_id](const AshEditor::VegetationPaletteViewEntry& entry)
		{
			return entry.species_id == original_id;
		});
	REQUIRE(restored != service.GetPaletteView()->end());
	CHECK(restored->species_digest == original_digest);
	REQUIRE(fixture.commands.RedoLatest());
	service.Tick(now + 3ms);

	CHECK_FALSE(service.RemovePaletteSpecies(original_id, false));
	const uint64_t before_remove = service.GetContentGeneration();
	REQUIRE(service.RemovePaletteSpecies(original_id, true));
	CHECK(service.GetContentGeneration() == before_remove + 1);
	REQUIRE(service.GetPaletteView()->size() == 1);
	CHECK(service.GetPaletteView()->front().species_id != original_id);
	CHECK(fixture.commands.RecordedCount() == 3);

	REQUIRE(fixture.commands.UndoLatest());
	service.Tick(now + 4ms);
	CHECK(service.GetContentGeneration() == before_remove + 2);
	REQUIRE(service.GetPaletteView()->size() == 2);
	REQUIRE(fixture.commands.RedoLatest());
	service.Tick(now + 5ms);
	CHECK(service.GetContentGeneration() == before_remove + 3);
	REQUIRE(service.GetPaletteView()->size() == 1);
}

TEST_CASE("Vegetation service synchronizes command mutations before every destructive intent")
{
	enum class Intent : uint8_t
	{
		Reload = 0,
		Open,
		Create
	};

	for (const Intent intent : { Intent::Reload, Intent::Open, Intent::Create })
	{
		const std::string suffix =
			intent == Intent::Reload ? "reload"
			: intent == Intent::Open ? "open"
			: "create";
		ServiceFixture fixture("service-command-window-" + suffix);
		fixture.root.Write(
			"vegetation/other.AshVegetationLayer",
			VegetationTest::ResolvedMinimalLayerBytes());
		REQUIRE(fixture.database.refresh());
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const uint64_t edited_generation = service.GetContentGeneration();
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestSave(now));
		fixture.executor.RunAll();
		service.Tick(now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);

		REQUIRE(fixture.commands.UndoLatest());
		const bool accepted =
			intent == Intent::Reload
				? service.RequestReload(now + 1ms)
			: intent == Intent::Open
				? service.OpenLayer("vegetation/other.AshVegetationLayer")
				: service.CreateLayer(
					"vegetation/new.AshVegetationLayer", 991);

		CHECK_FALSE(accepted);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::DirtyConflict);
		CHECK(service.GetContentGeneration() == edited_generation + 1);
		REQUIRE(service.GetPaletteView()->size() == 1);
		CHECK(service.GetPaletteView()->front().species_id ==
			VegetationTest::ResolvedMinimalPaletteEntry().species_id);
	}
}

TEST_CASE("Vegetation service reuses palette view for authoring-only command generations")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(68, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture(
		"service-authoring-only-command-view", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
	REQUIRE(service.AppendStrokePoint(
		VegetationTest::SurfaceRequest(0.5, 0.5)));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.EndStroke(now));
	fixture.executor.RunAll();
	service.Tick(now);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	REQUIRE(service.RequestSave(now + 1ms));
	fixture.executor.RunAll();
	service.Tick(now + 1ms);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);

	const std::filesystem::path species =
		fixture.root.Path() /
		"vegetation/Phase2ManualSpecies.AshVegetation";
	std::error_code remove_error{};
	REQUIRE(std::filesystem::remove(species, remove_error));
	REQUIRE_FALSE(remove_error);
	REQUIRE(fixture.database.refresh());
	REQUIRE(fixture.commands.UndoLatest());

	service.Tick(now + 2ms);

	CHECK(service.GetOperationState() !=
		AshEditor::VegetationOperationState::Failed);
	REQUIRE(service.GetPaletteView()->size() == 1);
	CHECK(service.GetPaletteView()->front().load_state ==
		AshEngine::AssetLoadState::Loaded);
	CHECK(service.GetCapabilities().can_save);
}

TEST_CASE("Vegetation service stable getters preserve the published view without external work")
{
	ServiceFixture fixture("service-stable-getter-publication");
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::shared_ptr<const AshEditor::VegetationPaletteView> view =
		service.GetPaletteView();
	const uint64_t generation = service.GetContentGeneration();
	const size_t inspect_calls = file_ops.inspect_calls;
	const size_t read_calls = file_ops.read_calls;

	for (size_t index = 0; index < 64; ++index)
	{
		CHECK(service.GetCapabilities().can_load);
		CHECK(service.GetPaletteView() == view);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		CHECK(service.GetContentGeneration() == generation);
	}

	CHECK(file_ops.inspect_calls == inspect_calls);
	CHECK(file_ops.read_calls == read_calls);
	CHECK(fixture.executor.TaskRecordCount() == 0);
	CHECK(fixture.executor.ScheduledOrderCount() == 0);
	CHECK(fixture.commands.RecordedCount() == 0);
}

TEST_CASE("Vegetation service handles every palette command record result")
{
	{
		ServiceFixture fixture("service-palette-recorded");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		fixture.commands.ScriptRecordResult(
			AshEditor::EditorCommandRecordResult::Recorded);

		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		REQUIRE(service.GetPaletteView()->size() == 2);
		CHECK(fixture.commands.RecordedCount() == 1);
	}

	{
		ServiceFixture fixture("service-palette-rolled-back-clean");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t before = service.GetContentGeneration();
		fixture.commands.ScriptRecordResult(
			AshEditor::EditorCommandRecordResult::RolledBack);

		CHECK_FALSE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() > before);
		REQUIRE(service.GetPaletteView()->size() == 1);
		CHECK(fixture.commands.RecordedCount() == 0);
		REQUIRE(service.RequestReload(std::chrono::steady_clock::now()));
		service.Shutdown();
	}

	{
		ServiceFixture fixture("service-palette-rollback-failed");
		fixture.root.Write(
			"vegetation/other.AshVegetationLayer",
			VegetationTest::ResolvedMinimalLayerBytes());
		REQUIRE(fixture.database.refresh());
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t before = service.GetContentGeneration();
		fixture.commands.ScriptRecordResult(
			AshEditor::EditorCommandRecordResult::RollbackFailed);

		CHECK_FALSE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() > before);
		REQUIRE(service.GetPaletteView()->size() == 2);
		CHECK(fixture.commands.RecordedCount() == 0);
		CHECK_FALSE(service.RequestReload(std::chrono::steady_clock::now()));
		CHECK_FALSE(service.OpenLayer(
			"vegetation/other.AshVegetationLayer"));
		REQUIRE(service.GetPaletteView()->size() == 2);
	}

	{
		ServiceFixture fixture("service-palette-rolled-back-dirty");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const uint64_t before = service.GetContentGeneration();
		fixture.commands.ScriptRecordResult(
			AshEditor::EditorCommandRecordResult::RolledBack);

		CHECK_FALSE(service.RemovePaletteSpecies(
			VegetationTest::ResolvedMinimalPaletteEntry().species_id, true));

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() > before);
		REQUIRE(service.GetPaletteView()->size() == 2);
		CHECK_FALSE(service.RequestReload(std::chrono::steady_clock::now()));
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::DirtyConflict);
	}
}

TEST_CASE("Vegetation service handles every stroke command record result")
{
	const std::array results{
		AshEditor::EditorCommandRecordResult::Recorded,
		AshEditor::EditorCommandRecordResult::RolledBack,
		AshEditor::EditorCommandRecordResult::RollbackFailed
	};
	for (size_t index = 0; index < results.size(); ++index)
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(
				static_cast<uint8_t>(69 + index), 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-stroke-record-result-" + std::to_string(index),
			true,
			&provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const AshEngine::VegetationLayerSnapshot baseline =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(
				fixture.root.Path() /
				"vegetation/test.AshVegetationLayer"));
		const uint64_t before = service.GetContentGeneration();
		fixture.commands.ScriptRecordResult(results[index]);
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		REQUIRE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.EndStroke(now));
		fixture.executor.RunAll();
		service.Tick(now);

		CHECK(service.GetContentGeneration() > before);
		CHECK(service.GetOperationState() ==
			(results[index] == AshEditor::EditorCommandRecordResult::Recorded
				? AshEditor::VegetationOperationState::Succeeded
				: AshEditor::VegetationOperationState::Failed));
		CHECK(fixture.commands.RecordedCount() ==
			(results[index] == AshEditor::EditorCommandRecordResult::Recorded
				? 1
				: 0));

		const std::filesystem::path copy =
			"vegetation/stroke-result-copy.AshVegetationLayer";
		REQUIRE(service.RequestSaveCopyAs(copy, now + 1ms));
		fixture.executor.RunAll();
		service.Tick(now + 1ms);
		const AshEngine::VegetationLayerSnapshot copied =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(
				fixture.root.Path() / copy));
		const bool content_restored =
			AuthoringBytesIgnoringGeneration(copied) ==
				AuthoringBytesIgnoringGeneration(baseline);
		CHECK(content_restored ==
			(results[index] ==
				AshEditor::EditorCommandRecordResult::RolledBack));

		const bool reload = service.RequestReload(now + 2ms);
		CHECK(reload ==
			(results[index] ==
				AshEditor::EditorCommandRecordResult::RolledBack));
		if (reload)
		{
			service.Shutdown();
		}
		else
		{
			CHECK(service.GetOperationState() ==
				AshEditor::VegetationOperationState::DirtyConflict);
		}
	}
}

TEST_CASE("Vegetation service Add and Replace undo redo restore exact authoring bytes")
{
	enum class Edit : uint8_t
	{
		Add = 0,
		Replace
	};
	for (const Edit edit : { Edit::Add, Edit::Replace })
	{
		ServiceFixture fixture(
			edit == Edit::Add
				? "service-add-exact-authoring"
				: "service-replace-exact-authoring");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const std::filesystem::path layer_path =
			fixture.root.Path() / "vegetation/test.AshVegetationLayer";
		const AshEngine::VegetationLayerSnapshot baseline =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(layer_path));
		const std::vector<uint8_t> baseline_authoring =
			AuthoringBytesIgnoringGeneration(baseline);
		uint64_t generation = service.GetContentGeneration();

		if (edit == Edit::Add)
		{
			REQUIRE(service.AddPaletteSpecies(
				"vegetation/SecondarySpecies.AshVegetation"));
		}
		else
		{
			REQUIRE(service.ReplacePaletteSpecies(
				VegetationTest::ResolvedMinimalPaletteEntry().species_id,
				"vegetation/Phase2ManualSpeciesReplacement.AshVegetation"));
		}
		CHECK(service.GetContentGeneration() == ++generation);
		const auto t0 = std::chrono::steady_clock::now();
		REQUIRE(service.RequestSave(t0));
		fixture.executor.RunAll();
		service.Tick(t0);
		const AshEngine::VegetationLayerSnapshot edited =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(layer_path));
		const std::vector<uint8_t> edited_authoring =
			AuthoringBytesIgnoringGeneration(edited);
		CHECK(edited_authoring != baseline_authoring);

		REQUIRE(fixture.commands.UndoLatest());
		CHECK(service.GetContentGeneration() == ++generation);
		REQUIRE(service.RequestSave(t0 + 1ms));
		fixture.executor.RunAll();
		service.Tick(t0 + 1ms);
		const AshEngine::VegetationLayerSnapshot undone =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(layer_path));
		CHECK(AuthoringBytesIgnoringGeneration(undone) ==
			baseline_authoring);

		REQUIRE(fixture.commands.RedoLatest());
		CHECK(service.GetContentGeneration() == ++generation);
		REQUIRE(service.RequestSave(t0 + 2ms));
		fixture.executor.RunAll();
		service.Tick(t0 + 2ms);
		const AshEngine::VegetationLayerSnapshot redone =
			DecodeLayerOrThrow(VegetationTest::ReadAllBytes(layer_path));
		CHECK(AuthoringBytesIgnoringGeneration(redone) ==
			edited_authoring);
	}
}

TEST_CASE("Vegetation service rejects every invalid stroke capture shape")
{
	enum class Scenario : uint8_t
	{
		Pending = 0,
		Failed,
		Outside,
		ReadyNull,
		PendingWithSnapshot,
		Throw
	};
	const std::array scenarios{
		Scenario::Pending,
		Scenario::Failed,
		Scenario::Outside,
		Scenario::ReadyNull,
		Scenario::PendingWithSnapshot,
		Scenario::Throw
	};
	for (size_t index = 0; index < scenarios.size(); ++index)
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(
				static_cast<uint8_t>(90 + index), 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = scenarios[index] == Scenario::ReadyNull
			? nullptr
			: snapshot;
		provider.throw_on_capture = scenarios[index] == Scenario::Throw;
		provider.attach_snapshot_to_non_ready =
			scenarios[index] == Scenario::PendingWithSnapshot;
		if (scenarios[index] != Scenario::ReadyNull &&
			scenarios[index] != Scenario::Throw)
		{
			provider.statuses.push_back(
				scenarios[index] == Scenario::Pending ||
					scenarios[index] == Scenario::PendingWithSnapshot
					? AshEngine::VegetationSurfaceStatus::Pending
				: scenarios[index] == Scenario::Failed
					? AshEngine::VegetationSurfaceStatus::Failed
					: AshEngine::VegetationSurfaceStatus::Outside);
		}

		ServiceFixture fixture(
			"service-invalid-stroke-capture-" + std::to_string(index),
			true,
			&provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		CHECK_FALSE(service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
		CHECK(fixture.executor.IsIdle());
	}
}

TEST_CASE("Vegetation service rejects every non-ready or malformed stroke batch")
{
	const std::array modes{
		ReadySurfaceSnapshot::BatchMode::Outside,
		ReadySurfaceSnapshot::BatchMode::Pending,
		ReadySurfaceSnapshot::BatchMode::Failed,
		ReadySurfaceSnapshot::BatchMode::Throw,
		ReadySurfaceSnapshot::BatchMode::Partial,
		ReadySurfaceSnapshot::BatchMode::WrongIndex
	};
	for (size_t index = 0; index < modes.size(); ++index)
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(
				static_cast<uint8_t>(110 + index), 1, 2, 3));
		snapshot->mode = modes[index];
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-invalid-stroke-batch-" + std::to_string(index),
			true,
			&provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		REQUIRE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.EndStroke(now));
		fixture.executor.RunAll();
		service.Tick(now);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
	}
}

TEST_CASE("Vegetation service rejects an expired stroke and a second batch failure")
{
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(121, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-expired-stroke", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		REQUIRE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		REQUIRE(service.EndStroke(
			std::chrono::steady_clock::time_point{}));
		fixture.executor.RunAll();
		service.Tick(std::chrono::steady_clock::time_point{});
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
	}

	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(122, 1, 2, 3));
		snapshot->mode = ReadySurfaceSnapshot::BatchMode::Failed;
		snapshot->fail_on_batch_call = 2;
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-second-stroke-batch", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		for (size_t index = 0; index < 4097; ++index)
		{
			REQUIRE(service.AppendStrokePoint(
				VegetationTest::SurfaceRequest(
					0.5 + static_cast<double>(index % 64) * 0.001,
					0.5 + static_cast<double>(index / 64) * 0.001)));
		}
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.EndStroke(now));
		fixture.executor.RunAll();
		service.Tick(now);
		REQUIRE(snapshot->batch_sizes.size() == 2);
		CHECK(snapshot->batch_sizes[0] == 4096);
		CHECK(snapshot->batch_sizes[1] == 1);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
	}
}

TEST_CASE("Vegetation service samples a stroke in stable bounded batches")
{
	const auto identity = VegetationTest::SurfaceIdentity(77, 1, 2, 3);
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(identity);
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-stroke", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));

	AshEngine::VegetationBrushStroke stroke = ValidStroke();
	const uint64_t before = service.GetContentGeneration();
	REQUIRE(service.BeginStroke(stroke, { 42 }));
	CHECK(provider.attempt_count == 1);
	std::vector<AshEngine::VegetationSurfaceSampleRequest> expected_requests{};
	expected_requests.reserve(4097);
	for (size_t index = 0; index < 4097; ++index)
	{
		const AshEngine::VegetationSurfaceSampleRequest request =
			VegetationTest::SurfaceRequest(
				static_cast<double>(index % 64) * 0.001,
				static_cast<double>(index / 64) * 0.001);
		expected_requests.push_back(request);
		REQUIRE(service.AppendStrokePoint(request));
	}
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.EndStroke(now));
	CHECK(provider.attempt_count == 1);
	fixture.executor.RunAll();
	service.Tick(now);

	REQUIRE(snapshot->batch_sizes.size() == 2);
	CHECK(snapshot->batch_sizes[0] == 4096);
	CHECK(snapshot->batch_sizes[1] == 1);
	REQUIRE(snapshot->batch_requests.size() == 2);
	std::vector<AshEngine::VegetationSurfaceSampleRequest> actual_requests =
		snapshot->batch_requests[0];
	actual_requests.insert(
		actual_requests.end(),
		snapshot->batch_requests[1].begin(),
		snapshot->batch_requests[1].end());
	REQUIRE(actual_requests.size() == expected_requests.size());
	for (size_t index = 0; index < expected_requests.size(); ++index)
	{
		CAPTURE(index);
		CHECK(actual_requests[index].chunk.x ==
			expected_requests[index].chunk.x);
		CHECK(actual_requests[index].chunk.z ==
			expected_requests[index].chunk.z);
		CHECK(actual_requests[index].local_xz.x ==
			expected_requests[index].local_xz.x);
		CHECK(actual_requests[index].local_xz.y ==
			expected_requests[index].local_xz.y);
	}
	CHECK(provider.attempt_count == 2);
	CHECK(service.GetContentGeneration() == before + 1);
	CHECK(fixture.commands.RecordedCount() == 1);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
}

TEST_CASE("Vegetation service discards a stroke when completion capture is stale")
{
	const auto first_identity = VegetationTest::SurfaceIdentity(81, 1, 2, 3);
	const auto first = std::make_shared<ReadySurfaceSnapshot>(first_identity);
	SequenceSurfaceProvider provider{};
	provider.snapshot = first;
	ServiceFixture fixture("service-stale-stroke", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));

	AshEngine::VegetationBrushStroke stroke =
		ValidStroke(AshEngine::VegetationBrushMode::Erase);
	const uint64_t before = service.GetContentGeneration();
	REQUIRE(service.BeginStroke(stroke, { 7 }));
	REQUIRE(service.AppendStrokePoint(VegetationTest::SurfaceRequest(0.0, 0.0)));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.EndStroke(now));
	fixture.executor.RunAll();
	provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(82, 1, 2, 3));
	service.Tick(now);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::SourceChanged);
	CHECK(service.GetContentGeneration() == before);
	CHECK(fixture.commands.RecordedCount() == 0);
}

TEST_CASE("Vegetation service rejects conflicting operations without side effects")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(131, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	const auto now = std::chrono::steady_clock::now();

	{
		ServiceFixture fixture("service-conflict-stroke", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(service.RequestSave(now));
		CHECK_FALSE(service.RequestReload(now));
		CHECK_FALSE(service.RequestReloadDiscard(true, now));
		CHECK_FALSE(service.RequestBake({ 42 }, now));
		CHECK_FALSE(service.CreateLayer(
			"vegetation/conflict.AshVegetationLayer", 1));
		CHECK(fixture.executor.IsIdle());
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
	}

	{
		ServiceFixture fixture("service-conflict-save", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.RequestSave(now));
		const uint64_t generation = service.GetContentGeneration();
		CHECK_FALSE(service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(service.RequestReload(now));
		CHECK_FALSE(service.RequestBake({ 42 }, now));
		CHECK_FALSE(service.CreateLayer(
			"vegetation/conflict.AshVegetationLayer", 1));
		CHECK(fixture.executor.PendingCount() == 1);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
		service.Shutdown();
		CHECK(fixture.executor.IsIdle());
		CHECK(fixture.executor.JoinedCount() == 1);
	}

	{
		ServiceFixture fixture("service-conflict-reload", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.RequestReload(now));
		const uint64_t generation = service.GetContentGeneration();
		CHECK_FALSE(service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(service.RequestSave(now));
		CHECK_FALSE(service.RequestBake({ 42 }, now));
		CHECK(fixture.executor.PendingCount() == 1);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
		service.Shutdown();
		CHECK(fixture.executor.IsIdle());
		CHECK(fixture.executor.JoinedCount() == 1);
	}

	{
		ServiceFixture fixture("service-conflict-bake", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.RequestBake({ 42 }, now));
		const uint64_t generation = service.GetContentGeneration();
		CHECK_FALSE(service.BeginStroke(ValidStroke(), { 42 }));
		CHECK_FALSE(service.RequestSave(now));
		CHECK_FALSE(service.RequestReload(now));
		CHECK_FALSE(service.CreateLayer(
			"vegetation/conflict.AshVegetationLayer", 1));
		CHECK(fixture.executor.PendingCount() == 1);
		CHECK(service.GetContentGeneration() == generation);
		CHECK(fixture.commands.RecordedCount() == 0);
		service.Shutdown();
		CHECK(fixture.executor.IsIdle());
		CHECK(fixture.executor.JoinedCount() == 1);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
	}
}

TEST_CASE("Vegetation service confirmed reload discards only the dirty document")
{
	ServiceFixture fixture("service-reload-discard");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const uint64_t opened_generation = service.GetContentGeneration();
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	REQUIRE(fixture.commands.RecordedCount() == 1);
	REQUIRE(service.GetPaletteView()->size() == 2);

	const auto now = std::chrono::steady_clock::now();
	CHECK_FALSE(service.RequestReloadDiscard(false, now));
	CHECK(fixture.executor.IsIdle());
	REQUIRE(service.RequestReloadDiscard(true, now));
	REQUIRE(fixture.executor.PendingCount() == 1);
	fixture.executor.RunAll();
	service.Tick(now);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(service.GetContentGeneration() == opened_generation);
	REQUIRE(service.GetPaletteView()->size() == 1);
	CHECK(fixture.commands.RecordedCount() == 0);
}

TEST_CASE("Vegetation service reload never overwrites a newer working-set generation")
{
	SUBCASE("clean reload does not overwrite an undo after worker completion")
	{
		ServiceFixture fixture("service-reload-clean-generation-race");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestSave(now));
		fixture.executor.RunAll();
		service.Tick(now);
		REQUIRE(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Succeeded);
		REQUIRE(service.GetPaletteView()->size() == 2);

		REQUIRE(service.RequestReload(now + 1ms));
		fixture.executor.RunAll();
		REQUIRE(fixture.commands.UndoLatest());
		const uint64_t undo_generation = service.GetContentGeneration();
		service.Tick(now + 1ms);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(service.GetContentGeneration() == undo_generation);
		service.Tick(now + 2ms);
		CHECK(service.GetPaletteView()->size() == 1);
	}

	SUBCASE("confirmed discard does not overwrite an undo after worker completion")
	{
		ServiceFixture fixture("service-reload-dirty-generation-race");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestReloadDiscard(true, now));
		fixture.executor.RunAll();
		REQUIRE(fixture.commands.UndoLatest());
		const uint64_t undo_generation = service.GetContentGeneration();
		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(service.GetContentGeneration() == undo_generation);
		service.Tick(now + 1ms);
		CHECK(service.GetPaletteView()->size() == 1);
	}
}

TEST_CASE("Vegetation service reload maps queued cancellation and deadline")
{
	SUBCASE("cancelled during the synchronous read")
	{
		ServiceFixture fixture("service-reload-cancelled");
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		file_ops.cancel_after_read = true;
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestReload(now));
		file_ops.cancellation = fixture.executor.CancellationState(
			fixture.executor.LastSubmittedTaskId());
		fixture.executor.RunAll();
		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Cancelled);
		CHECK(service.GetContentGeneration() == generation);
	}

	SUBCASE("expired before worker read")
	{
		ServiceFixture fixture("service-reload-timed-out");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const uint64_t generation = service.GetContentGeneration();
		const auto expired =
			std::chrono::steady_clock::now() - 31s;
		REQUIRE(service.RequestReload(expired));
		fixture.executor.RunAll();
		service.Tick(std::chrono::steady_clock::now());

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::TimedOut);
		CHECK(service.GetContentGeneration() == generation);
	}
}

TEST_CASE("Vegetation service save rejects source and generation races")
{
	{
		ServiceFixture fixture("service-save-source-race");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const uint64_t generation = service.GetContentGeneration();
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestSave(now));

		AshEngine::VegetationLayerSnapshot external =
			VegetationTest::ResolvedMinimalLayerSnapshot();
		external.content_generation = 77;
		external.layer_seed ^= 0x55aa55aa55aa55aaull;
		const std::vector<uint8_t> external_bytes =
			EncodeLayerOrThrow(external);
		fixture.root.Write(
			"vegetation/test.AshVegetationLayer", external_bytes);
		fixture.executor.RunAll();
		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(VegetationTest::ReadAllBytes(
			fixture.root.Path() /
				"vegetation/test.AshVegetationLayer") == external_bytes);
		CHECK(service.GetContentGeneration() == generation);
		CHECK_FALSE(service.GetCapabilities().can_save);
	}

	{
		ServiceFixture fixture("service-save-generation-race");
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const std::vector<uint8_t> original = VegetationTest::ReadAllBytes(
			fixture.root.Path() /
				"vegetation/test.AshVegetationLayer");
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestSave(now));
		fixture.executor.RunAll();
		REQUIRE(fixture.commands.UndoLatest());
		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(VegetationTest::ReadAllBytes(
			fixture.root.Path() /
				"vegetation/test.AshVegetationLayer") == original);
		CHECK(service.GetContentGeneration() > 1);
		CHECK(service.GetCapabilities().can_save);
	}
}

TEST_CASE("Vegetation service cancels a save during stage writes and cleans its stage")
{
	ServiceFixture fixture("service-cancel-save-write");
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::vector<uint8_t> original = VegetationTest::ReadAllBytes(
		fixture.root.Path() / "vegetation/test.AshVegetationLayer");
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	const uint64_t generation = service.GetContentGeneration();
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	const uint64_t task_id = fixture.executor.LastSubmittedTaskId();
	file_ops.cancellation = fixture.executor.CancellationState(task_id);
	REQUIRE(static_cast<bool>(file_ops.cancellation));

	fixture.executor.RunAll();
	service.Tick(now);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Cancelled);
	CHECK(service.GetContentGeneration() == generation);
	CHECK(VegetationTest::ReadAllBytes(
		fixture.root.Path() /
			"vegetation/test.AshVegetationLayer") == original);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	CHECK(service.GetCapabilities().can_save);
}

TEST_CASE("Vegetation service Save Copy As is create-new and does not rebind source")
{
	ServiceFixture fixture("service-save-copy-as");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::filesystem::path source =
		fixture.root.Path() / "vegetation/test.AshVegetationLayer";
	const std::filesystem::path destination =
		fixture.root.Path() / "vegetation/copy.AshVegetationLayer";
	const std::vector<uint8_t> source_bytes =
		VegetationTest::ReadAllBytes(source);
	const auto now = std::chrono::steady_clock::now();

	REQUIRE(service.RequestSaveCopyAs(
		"vegetation/copy.AshVegetationLayer", now));
	fixture.executor.RunAll();
	CHECK_FALSE(std::filesystem::exists(destination));
	service.Tick(now);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(VegetationTest::ReadAllBytes(destination) == source_bytes);
	CHECK(VegetationTest::ReadAllBytes(source) == source_bytes);

	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	REQUIRE(service.RequestSave(now + 1ms));
	fixture.executor.RunAll();
	service.Tick(now + 1ms);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	const std::vector<uint8_t> source_after_save =
		VegetationTest::ReadAllBytes(source);
	CHECK(source_after_save != source_bytes);
	CHECK(VegetationTest::ReadAllBytes(destination) == source_bytes);

	const std::vector<uint8_t> foreign{
		static_cast<uint8_t>('f'),
		static_cast<uint8_t>('o'),
		static_cast<uint8_t>('r'),
		static_cast<uint8_t>('e'),
		static_cast<uint8_t>('i'),
		static_cast<uint8_t>('g'),
		static_cast<uint8_t>('n')
	};
	fixture.root.Write(
		"vegetation/existing-copy.AshVegetationLayer", foreign);
	REQUIRE(service.RequestSaveCopyAs(
		"vegetation/existing-copy.AshVegetationLayer", now + 2ms));
	fixture.executor.RunAll();
	service.Tick(now + 2ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::AlreadyExists);
	CHECK(VegetationTest::ReadAllBytes(
		fixture.root.Path() /
			"vegetation/existing-copy.AshVegetationLayer") == foreign);
	CHECK(VegetationTest::ReadAllBytes(source) == source_after_save);
	CHECK(VegetationTest::ReadAllBytes(destination) == source_bytes);
	CHECK(service.GetCapabilities().can_save);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
}

TEST_CASE("Vegetation service Remove undo redo preserves exact authoring through save reload")
{
	ServiceFixture fixture("service-remove-roundtrip");
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::filesystem::path source =
		fixture.root.Path() / "vegetation/test.AshVegetationLayer";
	const AshEngine::VegetationLayerSnapshot original =
		DecodeLayerOrThrow(VegetationTest::ReadAllBytes(source));
	const AshEngine::VegetationId species_id =
		VegetationTest::ResolvedMinimalPaletteEntry().species_id;
	const uint64_t opened_generation = service.GetContentGeneration();

	CHECK_FALSE(service.RemovePaletteSpecies(species_id, false));
	CHECK(service.GetContentGeneration() == opened_generation);
	REQUIRE(service.RemovePaletteSpecies(species_id, true));
	const uint64_t removed_generation = service.GetContentGeneration();
	CHECK(removed_generation == opened_generation + 1);
	REQUIRE(fixture.commands.UndoLatest());
	service.Tick(std::chrono::steady_clock::now());
	const uint64_t undo_generation = service.GetContentGeneration();
	CHECK(undo_generation == removed_generation + 1);
	REQUIRE(service.GetPaletteView()->size() == 1);

	auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	fixture.executor.RunAll();
	service.Tick(now);
	const AshEngine::VegetationLayerSnapshot saved_undo =
		DecodeLayerOrThrow(VegetationTest::ReadAllBytes(source));
	CHECK(saved_undo.content_generation == undo_generation);
	CHECK(AuthoringBytesIgnoringGeneration(saved_undo) ==
		AuthoringBytesIgnoringGeneration(original));

	REQUIRE(fixture.commands.RedoLatest());
	service.Tick(now + 1ms);
	const uint64_t redo_generation = service.GetContentGeneration();
	CHECK(redo_generation == undo_generation + 1);
	REQUIRE(service.GetPaletteView()->empty());
	REQUIRE(service.RequestSave(now + 2ms));
	fixture.executor.RunAll();
	service.Tick(now + 2ms);
	const AshEngine::VegetationLayerSnapshot saved_redo =
		DecodeLayerOrThrow(VegetationTest::ReadAllBytes(source));
	CHECK(saved_redo.content_generation == redo_generation);
	CHECK(saved_redo.palette.empty());
	for (const AshEngine::VegetationLayerTile& tile : saved_redo.tiles)
	{
		for (const AshEngine::VegetationLayerPlane& plane : tile.planes)
		{
			CHECK(plane.kind !=
				AshEngine::VegetationLayerPlaneKind::SpeciesWeight);
		}
	}

	REQUIRE(service.RequestReload(now + 3ms));
	fixture.executor.RunAll();
	service.Tick(now + 3ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(service.GetContentGeneration() == redo_generation);
	CHECK(service.GetPaletteView()->empty());
	CHECK(fixture.commands.RecordedCount() == 0);
}

TEST_CASE("Vegetation service retains dirty evidence until matching bake commit")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(171, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-dirty-evidence", true, &provider);
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::filesystem::path active =
		fixture.root.Path() /
		"vegetation/test.AshVegetationLayer.AshVegetationChunks/active.asva";
	auto now = std::chrono::steady_clock::now();

	REQUIRE(service.RequestBake({ 42 }, now));
	fixture.executor.RunAll();
	service.Tick(now);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	REQUIRE(snapshot->batch_call_count > 0);
	const std::vector<uint8_t> baseline_active =
		VegetationTest::ReadAllBytes(active);

	const AshEngine::VegetationId species_id =
		VegetationTest::ResolvedMinimalPaletteEntry().species_id;
	REQUIRE(service.ReplacePaletteSpecies(
		species_id,
		"vegetation/Phase2ManualSpeciesReplacement.AshVegetation"));
	REQUIRE(fixture.commands.UndoLatest());
	service.Tick(now + 1ms);
	REQUIRE(fixture.commands.RedoLatest());
	service.Tick(now + 2ms);
	REQUIRE(service.RequestSave(now + 3ms));
	fixture.executor.RunAll();
	service.Tick(now + 3ms);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);

	snapshot->mode = ReadySurfaceSnapshot::BatchMode::Partial;
	snapshot->batch_call_count = 0;
	REQUIRE(service.RequestBake({ 42 }, now + 4ms));
	fixture.executor.RunAll();
	service.Tick(now + 4ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK(snapshot->batch_call_count > 0);
	CHECK(VegetationTest::ReadAllBytes(active) == baseline_active);

	snapshot->mode = ReadySurfaceSnapshot::BatchMode::Ready;
	snapshot->batch_call_count = 0;
	REQUIRE(service.RequestBake({ 42 }, now + 5ms));
	file_ops.cancellation = fixture.executor.CancellationState(
		fixture.executor.LastSubmittedTaskId());
	REQUIRE(static_cast<bool>(file_ops.cancellation));
	fixture.executor.RunAll();
	service.Tick(now + 5ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Cancelled);
	CHECK(snapshot->batch_call_count > 0);
	CHECK(VegetationTest::ReadAllBytes(active) == baseline_active);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	file_ops.cancellation.reset();

	snapshot->batch_call_count = 0;
	REQUIRE(service.RequestBake({ 42 }, now + 6ms));
	fixture.executor.RunAll();
	provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(171, 1, 2, 4));
	service.Tick(now + 6ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::SourceChanged);
	CHECK(snapshot->batch_call_count > 0);
	CHECK(VegetationTest::ReadAllBytes(active) == baseline_active);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	provider.snapshot = snapshot;

	snapshot->batch_call_count = 0;
	REQUIRE(service.RequestBake({ 42 }, now + 7ms));
	fixture.executor.RunAll();
	service.Tick(now + 7ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(snapshot->batch_call_count > 0);
	const std::vector<uint8_t> removed_active =
		VegetationTest::ReadAllBytes(active);
	CHECK(removed_active != baseline_active);

	snapshot->batch_call_count = 0;
	snapshot->batch_sizes.clear();
	REQUIRE(service.RequestBake({ 42 }, now + 8ms));
	fixture.executor.RunAll();
	service.Tick(now + 8ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(snapshot->batch_call_count == 0);
	CHECK(VegetationTest::ReadAllBytes(active) == removed_active);
}

TEST_CASE("Vegetation service session replacement uses full dirty on generation mismatch")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(172, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-session-full-dirty", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::filesystem::path active =
		fixture.root.Path() /
		"vegetation/test.AshVegetationLayer.AshVegetationChunks/active.asva";
	auto now = std::chrono::steady_clock::now();

	REQUIRE(service.RequestBake({ 42 }, now));
	fixture.executor.RunAll();
	service.Tick(now);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	const std::vector<uint8_t> baseline_active =
		VegetationTest::ReadAllBytes(active);

	AshEngine::VegetationLayerSnapshot replacement =
		VegetationTest::ResolvedMinimalLayerSnapshot();
	replacement.content_generation = 2;
	REQUIRE(replacement.palette.size() == 1);
	REQUIRE(replacement.tiles.size() == 1);
	REQUIRE(replacement.tiles.front().planes.size() == 2);
	CHECK(std::any_of(
		replacement.tiles.front().planes.front().values.begin(),
		replacement.tiles.front().planes.front().values.end(),
		[](const uint8_t value) { return value != 0; }));
	CHECK(std::any_of(
		replacement.tiles.front().planes.back().values.begin(),
		replacement.tiles.front().planes.back().values.end(),
		[](const uint8_t value) { return value != 0; }));
	fixture.root.Write(
		"vegetation/test.AshVegetationLayer",
		EncodeLayerOrThrow(replacement));
	REQUIRE(service.RequestReload(now + 1ms));
	fixture.executor.RunAll();
	service.Tick(now + 1ms);
	REQUIRE(service.GetContentGeneration() == 2);
	REQUIRE(service.GetPaletteView()->size() == 1);

	snapshot->batch_call_count = 0;
	snapshot->batch_sizes.clear();
	REQUIRE(service.RequestBake({ 42 }, now + 2ms));
	fixture.executor.RunAll();
	service.Tick(now + 2ms);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(snapshot->batch_call_count > 0);
	CHECK_FALSE(snapshot->batch_sizes.empty());
	CHECK(VegetationTest::ReadAllBytes(active) != baseline_active);
	const auto resolver =
		fixture.database.capture_vegetation_resolver_snapshot();
	REQUIRE(static_cast<bool>(resolver));
	const AshEngine::VegetationActiveChunkSetReadResult read =
		AshEngine::read_active_vegetation_chunk_set(
			fixture.root.Path(),
			"vegetation/test.AshVegetationLayer",
			*resolver,
			AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget(),
			VegetationTest::ActiveControl(1s));
	REQUIRE(read.status ==
		AshEngine::VegetationActiveChunkSetReadStatus::Succeeded);
	REQUIRE(static_cast<bool>(read.snapshot));
	CHECK(read.snapshot->layer_generation == 2);
	REQUIRE(read.snapshot->entries.size() == 1);
	CHECK(read.snapshot->entries.front().coord.x == -1);
	CHECK(read.snapshot->entries.front().coord.z == 0);
	REQUIRE(read.snapshot->entries.front().referenced_species_ids.size() == 1);
	CHECK(read.snapshot->entries.front().referenced_species_ids.front() ==
		replacement.palette.front().species_id);
}

TEST_CASE("Vegetation service reports retained cleanup ownership at shutdown")
{
	ServiceFixture fixture("service-retained-cleanup");
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.allow_remove = false;
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	file_ops.cancellation = fixture.executor.CancellationState(
		fixture.executor.LastSubmittedTaskId());
	REQUIRE(static_cast<bool>(file_ops.cancellation));
	fixture.executor.RunAll();
	service.Tick(now);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK(HasOwnedOperationStage(fixture.root.Path()));
	CHECK(file_ops.remove_file_calls > 0);

	service.Shutdown();
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK(fixture.executor.IsIdle());
	CHECK(HasOwnedOperationStage(fixture.root.Path()));
	CHECK(file_ops.remove_file_calls > 1);
}

TEST_CASE("Vegetation service blocks later operations until retained cleanup recovers")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(146, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture(
		"service-retained-cleanup-recovery", true, &provider);
	CancellingFileOps file_ops(
		AshEngine::get_default_vegetation_file_ops());
	file_ops.allow_remove = false;
	fixture.deps.pFileOps = &file_ops;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.AddPaletteSpecies(
		"vegetation/SecondarySpecies.AshVegetation"));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.RequestSave(now));
	file_ops.cancellation = fixture.executor.CancellationState(
		fixture.executor.LastSubmittedTaskId());
	REQUIRE(static_cast<bool>(file_ops.cancellation));
	fixture.executor.RunAll();
	service.Tick(now);
	REQUIRE(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	REQUIRE(HasOwnedOperationStage(fixture.root.Path()));

	const auto blocked_capabilities = service.GetCapabilities();
	CHECK_FALSE(blocked_capabilities.can_create);
	CHECK_FALSE(blocked_capabilities.can_load);
	CHECK_FALSE(blocked_capabilities.can_save);
	CHECK_FALSE(blocked_capabilities.can_save_copy_as);
	CHECK_FALSE(blocked_capabilities.can_reload);
	CHECK_FALSE(blocked_capabilities.can_edit_palette);
	CHECK_FALSE(blocked_capabilities.can_paint);
	CHECK_FALSE(blocked_capabilities.can_erase);
	CHECK_FALSE(blocked_capabilities.can_bake);
	const uint64_t blocked_generation = service.GetContentGeneration();
	const AshEngine::VegetationId original_id =
		VegetationTest::ResolvedMinimalPaletteEntry().species_id;
	CHECK_FALSE(service.ReplacePaletteSpecies(
		original_id,
		"vegetation/Phase2ManualSpeciesReplacement.AshVegetation"));
	CHECK(service.GetContentGeneration() == blocked_generation);
	CHECK_FALSE(service.BeginStroke(ValidStroke(), { 42 }));
	CHECK_FALSE(service.RequestSave(now + 1ms));
	CHECK_FALSE(service.RequestReload(now + 1ms));
	CHECK_FALSE(service.RequestBake({ 42 }, now + 1ms));
	CHECK(provider.attempt_count == 0);
	CHECK(fixture.executor.PendingCount() == 0);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);

	file_ops.allow_remove = true;
	service.Tick(now + 2ms);
	REQUIRE_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	const auto recovered_capabilities = service.GetCapabilities();
	CHECK_FALSE(recovered_capabilities.can_create);
	CHECK(recovered_capabilities.can_load);
	CHECK(recovered_capabilities.can_save);
	CHECK(recovered_capabilities.can_save_copy_as);
	CHECK(recovered_capabilities.can_reload);
	CHECK(recovered_capabilities.can_edit_palette);
	CHECK(recovered_capabilities.can_paint);
	CHECK(recovered_capabilities.can_erase);
	CHECK(recovered_capabilities.can_bake);
	REQUIRE(service.RequestSave(now + 3ms));
	file_ops.cancellation.reset();
	fixture.executor.RunAll();
	service.Tick(now + 3ms);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
}

TEST_CASE("Vegetation service Create Layer rejects unsafe paths and identities")
{
	{
		ServiceFixture fixture("service-create-paths", false);
		fixture.root.Write(
			"vegetation/existing.AshVegetationLayer",
			VegetationTest::ResolvedMinimalLayerBytes());
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		CHECK_FALSE(service.CreateLayer(
			"vegetation/existing.AshVegetationLayer", 1));
		CHECK_FALSE(service.CreateLayer(
			"../escape.AshVegetationLayer", 1));
		CHECK_FALSE(service.CreateLayer(
			"vegetation/wrong.extension", 1));
		CHECK(service.GetContentGeneration() == 0);
		CHECK(fixture.commands.RecordedCount() == 0);
	}

	{
		ServiceFixture fixture("service-create-zero-id", false);
		fixture.deps.create_layer_id = []
		{
			return AshEngine::VegetationId{};
		};
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		CHECK_FALSE(service.CreateLayer(
			"vegetation/zero.AshVegetationLayer", 1));
		CHECK(service.GetContentGeneration() == 0);
	}

	{
		ServiceFixture fixture("service-create-colliding-id");
		const AshEngine::VegetationId collision =
			VegetationTest::ResolvedMinimalLayerSnapshot().layer_id;
		fixture.deps.create_layer_id = [collision]
		{
			return collision;
		};
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		CHECK_FALSE(service.CreateLayer(
			"vegetation/collision.AshVegetationLayer", 1));
		CHECK(service.GetContentGeneration() == 1);
	}
}

TEST_CASE("Vegetation service contains InspectPath exceptions at public boundaries")
{
	SUBCASE("Create Layer")
	{
		ServiceFixture fixture("service-inspect-throw-create", false);
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		file_ops.throw_on_inspect = true;
		bool created = true;
		CHECK_NOTHROW(created = service.CreateLayer(
			"vegetation/new.AshVegetationLayer", 1));
		CHECK_FALSE(created);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Idle);
		CHECK(service.GetContentGeneration() == 0);
	}

	SUBCASE("initial bake inspection")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(147, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-inspect-throw-bake-request", true, &provider);
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		file_ops.throw_on_inspect = true;
		bool requested = true;
		CHECK_NOTHROW(requested = service.RequestBake(
			{ 42 }, std::chrono::steady_clock::now()));
		CHECK_FALSE(requested);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(fixture.executor.IsIdle());
		CHECK(provider.attempt_count == 0);
	}

	SUBCASE("bake completion inspection")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(148, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-inspect-throw-bake-completion", true, &provider);
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		file_ops.throw_on_inspect = true;
		CHECK_NOTHROW(service.Tick(now));
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK(fixture.executor.JoinedCount() == 1);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}
}

TEST_CASE("Vegetation service shutdown cancels stroke sampling without mutation")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(141, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-shutdown-stroke", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const uint64_t generation = service.GetContentGeneration();
	REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
	REQUIRE(service.AppendStrokePoint(
		VegetationTest::SurfaceRequest(0.5, 0.5)));
	REQUIRE(service.EndStroke(std::chrono::steady_clock::now()));

	service.Shutdown();

	CHECK(fixture.executor.IsIdle());
	CHECK(fixture.executor.JoinedCount() == 1);
	CHECK(service.GetContentGeneration() == generation);
	CHECK(fixture.commands.RecordedCount() == 0);
	CHECK(snapshot->batch_call_count == 0);
}

TEST_CASE("Vegetation service shutdown precedes production executor destruction")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(143, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture(
		"service-production-executor-lifetime", true, &provider);
	AshEditor::VegetationEditorTaskExecutor executor{};
	fixture.deps.pTaskExecutor = &executor;
	{
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
		REQUIRE(service.AppendStrokePoint(
			VegetationTest::SurfaceRequest(0.5, 0.5)));
		REQUIRE(service.EndStroke(std::chrono::steady_clock::now()));
		service.Shutdown();
		CHECK(executor.IsIdle());
	}
	CHECK(executor.IsIdle());
}

TEST_CASE("Vegetation service shutdown observes a manual task exception before Join")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(151, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture(
		"service-shutdown-manual-exception", true, &provider);
	ThrowingTaskExecutorAdapter throwing_executor(
		fixture.executor, false);
	fixture.deps.pTaskExecutor = &throwing_executor;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
	REQUIRE(service.AppendStrokePoint(
		VegetationTest::SurfaceRequest(0.5, 0.5)));
	REQUIRE(service.EndStroke(std::chrono::steady_clock::now()));
	const uint64_t task_id = throwing_executor.LastSubmittedTask();
	REQUIRE(task_id != 0);
	fixture.executor.RunAll();
	REQUIRE(fixture.executor.IsComplete(task_id));
	REQUIRE(fixture.executor.GetException(task_id) != nullptr);

	service.Shutdown();

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK_FALSE(fixture.executor.IsComplete(task_id));
	CHECK(fixture.executor.GetException(task_id) == nullptr);
	CHECK(fixture.executor.TaskRecordCount() == 0);
	CHECK(fixture.executor.ScheduledOrderCount() == 0);
}

TEST_CASE("Vegetation service shutdown observes a production task exception before Join")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(152, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture(
		"service-shutdown-production-exception", true, &provider);
	AshEditor::VegetationEditorTaskExecutor production_executor{};
	ThrowingTaskExecutorAdapter throwing_executor(
		production_executor, true);
	fixture.deps.pTaskExecutor = &throwing_executor;
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
	REQUIRE(service.AppendStrokePoint(
		VegetationTest::SurfaceRequest(0.5, 0.5)));
	REQUIRE(service.EndStroke(std::chrono::steady_clock::now()));
	const uint64_t task_id = throwing_executor.LastSubmittedTask();
	REQUIRE(task_id != 0);

	service.Shutdown();

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Failed);
	CHECK_FALSE(production_executor.IsComplete(task_id));
	CHECK(production_executor.GetException(task_id) == nullptr);
	CHECK(production_executor.IsIdle());
}

TEST_CASE("Vegetation service observes cancellation during stroke sampling")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(142, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-cancel-during-stroke", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const uint64_t generation = service.GetContentGeneration();
	REQUIRE(service.BeginStroke(ValidStroke(), { 42 }));
	REQUIRE(service.AppendStrokePoint(
		VegetationTest::SurfaceRequest(0.5, 0.5)));
	const auto now = std::chrono::steady_clock::now();
	REQUIRE(service.EndStroke(now));
	const uint64_t task_id = fixture.executor.LastSubmittedTaskId();
	const std::shared_ptr<std::atomic_bool> cancellation =
		fixture.executor.CancellationState(task_id);
	REQUIRE(static_cast<bool>(cancellation));
	snapshot->on_batch = [cancellation]
	{
		cancellation->store(true, std::memory_order_release);
	};

	fixture.executor.RunAll();
	service.Tick(now);

	CHECK(snapshot->batch_call_count == 1);
	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Cancelled);
	CHECK(service.GetContentGeneration() == generation);
	CHECK(fixture.commands.RecordedCount() == 0);
}

TEST_CASE("Vegetation service bake publishes active only on the main thread")
{
	const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
		VegetationTest::SurfaceIdentity(151, 1, 2, 3));
	SequenceSurfaceProvider provider{};
	provider.snapshot = snapshot;
	ServiceFixture fixture("service-bake-main-thread", true, &provider);
	AshEditor::VegetationEditorService service(std::move(fixture.deps));
	REQUIRE(service.Initialize());
	REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
	const std::filesystem::path active =
		fixture.root.Path() /
		"vegetation/test.AshVegetationLayer.AshVegetationChunks/active.asva";
	const auto now = std::chrono::steady_clock::now();

	REQUIRE(service.RequestBake({ 42 }, now));
	CHECK_FALSE(std::filesystem::exists(active));
	fixture.executor.RunAll();
	CHECK_FALSE(std::filesystem::exists(active));
	service.Tick(now);

	CHECK(service.GetOperationState() ==
		AshEditor::VegetationOperationState::Succeeded);
	CHECK(std::filesystem::is_regular_file(active));
}

TEST_CASE("Vegetation service preserves exact bake cancellation and timeout states")
{
	SUBCASE("active read cancellation")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(152, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-active-read-cancel", true, &provider);
		CancellingFileOps file_ops(
			AshEngine::get_default_vegetation_file_ops());
		fixture.deps.pFileOps = &file_ops;
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		file_ops.cancellation = fixture.executor.CancellationState(
			fixture.executor.LastSubmittedTaskId());
		file_ops.cancel_after_inspect = true;
		fixture.executor.RunAll();
		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Cancelled);
		CHECK(snapshot->batch_call_count == 0);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}

	SUBCASE("active read deadline")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(153, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-active-read-timeout", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto expired =
			std::chrono::steady_clock::now() - 31s;
		REQUIRE(service.RequestBake({ 42 }, expired));
		fixture.executor.RunAll();
		service.Tick(std::chrono::steady_clock::now());

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::TimedOut);
		CHECK(snapshot->batch_call_count == 0);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}

	SUBCASE("sampling cancellation")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(154, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-sampling-cancel", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		const auto cancellation = fixture.executor.CancellationState(
			fixture.executor.LastSubmittedTaskId());
		REQUIRE(static_cast<bool>(cancellation));
		snapshot->on_batch = [cancellation]
		{
			cancellation->store(true, std::memory_order_release);
		};
		fixture.executor.RunAll();
		service.Tick(now);

		CHECK(snapshot->batch_call_count == 1);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Cancelled);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}

	SUBCASE("worker deadline is already expired before sampling")
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(155, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-sampling-timeout", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto expired =
			std::chrono::steady_clock::now() - 31s;
		REQUIRE(service.RequestBake({ 42 }, expired));
		fixture.executor.RunAll();
		service.Tick(std::chrono::steady_clock::now());

		CHECK(snapshot->batch_call_count == 0);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::TimedOut);
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}
}

TEST_CASE("Vegetation service bake rechecks source Layer revision and native identity")
{
	enum class Race : uint8_t
	{
		InPlaceBytes = 0,
		ReplaceSameBytes,
		Delete
	};
	const std::array<Race, 3> races{
		Race::InPlaceBytes,
		Race::ReplaceSameBytes,
		Race::Delete
	};
	for (size_t index = 0; index < races.size(); ++index)
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(
				static_cast<uint8_t>(152 + index), 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-layer-race-" + std::to_string(index),
			true,
			&provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const std::filesystem::path layer =
			fixture.root.Path() / "vegetation/test.AshVegetationLayer";
		const std::filesystem::path active =
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/active.asva";
		const std::vector<uint8_t> original =
			VegetationTest::ReadAllBytes(layer);
		const auto now = std::chrono::steady_clock::now();

		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		CHECK_FALSE(std::filesystem::exists(active));
		switch (races[index])
		{
		case Race::InPlaceBytes:
			fixture.root.Write(
				"vegetation/test.AshVegetationLayer",
				VegetationTest::DifferentValidLayerBytes());
			break;
		case Race::ReplaceSameBytes:
			REQUIRE(std::filesystem::remove(layer));
			fixture.root.Write(
				"vegetation/test.AshVegetationLayer", original);
			break;
		case Race::Delete:
			REQUIRE(std::filesystem::remove(layer));
			break;
		}

		service.Tick(now);

		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK_FALSE(std::filesystem::exists(active));
		CHECK_FALSE(HasOwnedOperationStage(fixture.root.Path()));
	}
}

TEST_CASE("Vegetation service bake rejects malformed samples and stale identities")
{
	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(161, 1, 2, 3));
		snapshot->mode = ReadySurfaceSnapshot::BatchMode::Partial;
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-bake-malformed-samples", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		service.Tick(now);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::Failed);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
	}

	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(162, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-bake-stale-surface", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		provider.snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(162, 1, 2, 4));
		service.Tick(now);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
	}

	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(163, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture("service-bake-stale-species", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		fixture.root.Write(
			"vegetation/Phase2ManualSpecies.AshVegetation",
			VegetationTest::ReadFixtureBytes(
				"project/src/tests/fixtures/vegetation/"
				"Phase2ManualSpeciesReplacement.AshVegetation"));
		REQUIRE(fixture.database.refresh());
		service.Tick(now);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
	}

	{
		const auto snapshot = std::make_shared<ReadySurfaceSnapshot>(
			VegetationTest::SurfaceIdentity(164, 1, 2, 3));
		SequenceSurfaceProvider provider{};
		provider.snapshot = snapshot;
		ServiceFixture fixture(
			"service-bake-stale-layer-generation", true, &provider);
		AshEditor::VegetationEditorService service(std::move(fixture.deps));
		REQUIRE(service.Initialize());
		REQUIRE(service.OpenLayer("vegetation/test.AshVegetationLayer"));
		REQUIRE(service.AddPaletteSpecies(
			"vegetation/SecondarySpecies.AshVegetation"));
		const auto now = std::chrono::steady_clock::now();
		REQUIRE(service.RequestBake({ 42 }, now));
		fixture.executor.RunAll();
		REQUIRE(fixture.commands.UndoLatest());
		service.Tick(now);
		CHECK(service.GetOperationState() ==
			AshEditor::VegetationOperationState::SourceChanged);
		CHECK_FALSE(std::filesystem::exists(
			fixture.root.Path() /
			"vegetation/test.AshVegetationLayer.AshVegetationChunks/"
				"active.asva"));
	}
}
