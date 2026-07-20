#include "Core/EditorCommand.h"
#include "Core/EditorContext.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/EditorSelection.h"
#include "Core/VegetationCommands.h"
#include "Function/Asset/VegetationBrush.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"
#include "Vegetation/VegetationTestSupport.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	class CountingDocumentCommand : public AshEditor::EditorCommand
	{
	public:
		CountingDocumentCommand(
			int& value,
			AshEditor::EditorCommandDocumentKey key,
			const bool undo_succeeds = true)
			: _value(value)
			, _key(std::move(key))
			, _undo_succeeds(undo_succeeds)
		{
		}

		const char* GetLabel() const override
		{
			return "Counting document command";
		}

		bool Execute(AshEditor::EditorContext&) override
		{
			++_value;
			return true;
		}

		bool Undo(AshEditor::EditorContext&) override
		{
			if (!_undo_succeeds)
			{
				return false;
			}
			--_value;
			return true;
		}

		std::optional<AshEditor::EditorCommandDocumentKey> GetDocumentKey() const override
		{
			return _key;
		}

	private:
		int& _value;
		AshEditor::EditorCommandDocumentKey _key{};
		bool _undo_succeeds = true;
	};

	class UnscopedCommand final : public AshEditor::EditorCommand
	{
	public:
		const char* GetLabel() const override
		{
			return "Unscoped command";
		}

		bool Execute(AshEditor::EditorContext&) override
		{
			return true;
		}

		bool Undo(AshEditor::EditorContext&) override
		{
			return true;
		}
	};

	class ClearSelectionDocumentCommand final : public CountingDocumentCommand
	{
	public:
		using CountingDocumentCommand::CountingDocumentCommand;

		AshEditor::EditorCommandSelection GetSelectionAfterExecute() const override
		{
			return AshEditor::EditorCommandSelection::Clear();
		}
	};

	class ClearSelectionOnUndoDocumentCommand final : public CountingDocumentCommand
	{
	public:
		using CountingDocumentCommand::CountingDocumentCommand;

		AshEditor::EditorCommandSelection GetSelectionAfterUndo() const override
		{
			return AshEditor::EditorCommandSelection::Clear();
		}
	};

	class ThrowingMergeCommand final : public CountingDocumentCommand
	{
	public:
		using CountingDocumentCommand::CountingDocumentCommand;

		void Arm()
		{
			_bThrowOnMerge = true;
		}

		bool TryMerge(const AshEditor::EditorCommand&) override
		{
			if (_bThrowOnMerge)
			{
				throw std::runtime_error("already-executed commands must not invoke TryMerge");
			}
			return false;
		}

	private:
		bool _bThrowOnMerge = false;
	};

	class MergeableCountingCommand final : public AshEditor::EditorCommand
	{
	public:
		MergeableCountingCommand(
			int& value,
			AshEditor::EditorCommandDocumentKey key)
			: _value(value)
			, _key(std::move(key))
		{
		}

		const char* GetLabel() const override
		{
			return "Mergeable counting command";
		}

		bool Execute(AshEditor::EditorContext&) override
		{
			_value += _delta;
			return true;
		}

		bool Undo(AshEditor::EditorContext&) override
		{
			_value -= _delta;
			return true;
		}

		bool TryMerge(const AshEditor::EditorCommand& refSubsequentCommand) override
		{
			const auto* pSubsequent = dynamic_cast<const MergeableCountingCommand*>(&refSubsequentCommand);
			if (!pSubsequent || pSubsequent->_key != _key)
			{
				return false;
			}
			_delta += pSubsequent->_delta;
			return true;
		}

		std::optional<AshEditor::EditorCommandDocumentKey> GetDocumentKey() const override
		{
			return _key;
		}

	private:
		int& _value;
		AshEditor::EditorCommandDocumentKey _key{};
		int _delta = 1;
	};

	AshEngine::VegetationBrushStroke MakeSingleTexelEraseStroke()
	{
		AshEngine::VegetationBrushStroke stroke{};
		stroke.mode = AshEngine::VegetationBrushMode::Erase;
		stroke.radius_mm = 250;
		stroke.strength = 1;
		stroke.falloff = 0;
		stroke.spacing_mm = 1;
		stroke.path.push_back(VegetationTest::SurfaceRequest(-63.5, 96.5));
		return stroke;
	}

	AshEngine::VegetationLayerSnapshot MakeCommandLayerSnapshot()
	{
		return VegetationTest::MinimalLayerSnapshot();
	}

	AshEngine::VegetationPaletteEdit MakePaletteAdd()
	{
		AshEngine::VegetationPaletteEdit edit{};
		edit.mode = AshEngine::VegetationPaletteEditMode::Add;
		edit.replacement = VegetationTest::MinimalPaletteEntry();
		edit.replacement.species_id = VegetationTest::SequentialId(101);
		edit.replacement.species_sha256.fill(0x7c);
		edit.replacement.species_asset_path = "vegetation/HistoryIntervening.AshVegetation";
		return edit;
	}
}

TEST_CASE("Vegetation already-executed command records without executing twice and rolls back rejection")
{
	AshEditor::UndoRedoService history{};
	AshEditor::EditorContext context{};
	AshEditor::SelectionService selection{};
	context.pSelectionService = &selection;
	const AshEditor::EditorCommandDocumentKey key{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};

	int value = 1;
	selection.Select({ AshEditor::EditorSelectionKind::Asset, 7u, "Layer", "vegetation/meadow" });
	CHECK(history.RecordExecutedCommand(
		std::make_unique<ClearSelectionDocumentCommand>(value, key), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	CHECK(value == 1);
	CHECK_FALSE(selection.HasSelection());
	REQUIRE(history.Undo(context));
	CHECK(value == 0);
	REQUIRE(history.Redo(context));
	CHECK(value == 1);

	CHECK(history.RecordExecutedCommand(nullptr, context) ==
		AshEditor::EditorCommandRecordResult::RollbackFailed);
	CHECK(value == 1);

	REQUIRE(history.BeginTransaction("Existing transaction"));
	int rollback_value = 1;
	selection.Select({ AshEditor::EditorSelectionKind::Asset, 8u, "Layer", "vegetation/meadow" });
	CHECK(history.RecordExecutedCommand(
		std::make_unique<ClearSelectionOnUndoDocumentCommand>(rollback_value, key), context) ==
		AshEditor::EditorCommandRecordResult::RolledBack);
	CHECK(rollback_value == 0);
	CHECK_FALSE(selection.HasSelection());
	history.CancelTransaction(context);

	REQUIRE(history.BeginTransaction("Existing transaction"));
	int failed_rollback_value = 1;
	CHECK(history.RecordExecutedCommand(
		std::make_unique<CountingDocumentCommand>(failed_rollback_value, key, false), context) ==
		AshEditor::EditorCommandRecordResult::RollbackFailed);
	CHECK(failed_rollback_value == 1);
	history.CancelTransaction(context);
}

TEST_CASE("Vegetation already-executed recording isolates merge and observer failures")
{
	const AshEditor::EditorCommandDocumentKey key{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};
	AshEditor::EditorContext context{};

	SUBCASE("already-executed entries never call virtual merge")
	{
		AshEditor::UndoRedoService history{};
		int previous_value = 0;
		auto previous = std::make_unique<ThrowingMergeCommand>(previous_value, key);
		ThrowingMergeCommand* previous_view = previous.get();
		REQUIRE(history.Execute(std::move(previous), context));
		int redo_value = 0;
		REQUIRE(history.Execute(
			std::make_unique<CountingDocumentCommand>(redo_value, key), context));
		REQUIRE(history.Undo(context));
		CHECK(history.CanRedo());
		previous_view->Arm();

		int already_applied_value = 1;
		AshEditor::EditorCommandRecordResult result =
			AshEditor::EditorCommandRecordResult::RollbackFailed;
		CHECK_NOTHROW(result = history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(already_applied_value, key), context));
		CHECK(result == AshEditor::EditorCommandRecordResult::Recorded);
		CHECK(already_applied_value == 1);
		CHECK_FALSE(history.CanRedo());
		REQUIRE(history.Undo(context));
		CHECK(already_applied_value == 0);
		REQUIRE(history.Undo(context));
		CHECK(previous_value == 0);
	}

	SUBCASE("observer exceptions cannot escape after the history entry commits")
	{
		AshEditor::EditorEventBus event_bus{};
		AshEditor::UndoRedoService history{};
		history.SetEventBus(&event_bus);
		const AshEditor::EditorEventSubscriptionId subscription =
			event_bus.Subscribe<AshEditor::EditorUndoHistoryChangedEvent>(
				[](const AshEditor::EditorUndoHistoryChangedEvent&)
				{
					throw std::runtime_error("observer failure");
				});
		REQUIRE(subscription != 0u);

		int already_applied_value = 1;
		AshEditor::EditorCommandRecordResult result =
			AshEditor::EditorCommandRecordResult::RollbackFailed;
		CHECK_NOTHROW(result = history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(already_applied_value, key), context));
		CHECK(result == AshEditor::EditorCommandRecordResult::Recorded);
		CHECK(history.CanUndo());
		CHECK(event_bus.Unsubscribe(subscription));
		REQUIRE(history.Undo(context));
		CHECK(already_applied_value == 0);
	}
}

TEST_CASE("Vegetation document history removal preserves unrelated undo redo and dirty state")
{
	const AshEditor::EditorCommandDocumentKey meadow{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};
	const AshEditor::EditorCommandDocumentKey forest{
		"vegetation-layer",
		"vegetation/forest.ashvegetationlayer"
	};

	AshEditor::CompositeCommand homogeneous("Homogeneous");
	int composite_value = 0;
	homogeneous.Append(std::make_unique<CountingDocumentCommand>(composite_value, meadow));
	homogeneous.Append(std::make_unique<CountingDocumentCommand>(composite_value, meadow));
	REQUIRE(homogeneous.GetDocumentKey().has_value());
	CHECK(*homogeneous.GetDocumentKey() == meadow);

	AshEditor::CompositeCommand mixed("Mixed");
	mixed.Append(std::make_unique<CountingDocumentCommand>(composite_value, meadow));
	mixed.Append(std::make_unique<CountingDocumentCommand>(composite_value, forest));
	CHECK_FALSE(mixed.GetDocumentKey().has_value());

	AshEditor::CompositeCommand partly_unscoped("Partly unscoped");
	partly_unscoped.Append(std::make_unique<CountingDocumentCommand>(composite_value, meadow));
	partly_unscoped.Append(std::make_unique<UnscopedCommand>());
	CHECK_FALSE(partly_unscoped.GetDocumentKey().has_value());

	AshEditor::UndoRedoService history{};
	AshEditor::EditorContext context{};
	int meadow_value = 1;
	REQUIRE(history.RecordExecutedCommand(
		std::make_unique<CountingDocumentCommand>(meadow_value, meadow), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	history.MarkSaved();
	CHECK_FALSE(history.IsDirty());

	int forest_value = 1;
	REQUIRE(history.RecordExecutedCommand(
		std::make_unique<CountingDocumentCommand>(forest_value, forest), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	++meadow_value;
	REQUIRE(history.RecordExecutedCommand(
		std::make_unique<CountingDocumentCommand>(meadow_value, meadow), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	REQUIRE(history.Undo(context));
	CHECK(meadow_value == 1);
	CHECK(history.CanRedo());
	CHECK(history.IsDirty());

	CHECK(history.RemoveCommandsForDocument(meadow) == 2u);
	CHECK_FALSE(history.CanRedo());
	CHECK(history.CanUndo());
	CHECK(history.IsDirty());
	REQUIRE(history.Undo(context));
	CHECK(forest_value == 0);
	CHECK_FALSE(history.CanUndo());
	CHECK_FALSE(history.IsDirty());

	forest_value = 1;
	REQUIRE(history.RecordExecutedCommand(
		std::make_unique<CountingDocumentCommand>(forest_value, forest), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	CHECK(history.IsDirty());
	REQUIRE(history.Undo(context));
	CHECK(forest_value == 0);
	CHECK_FALSE(history.IsDirty());
	CHECK(history.RemoveCommandsForDocument(meadow) == 0u);

	REQUIRE(history.BeginTransaction("Open transaction"));
	CHECK(history.RemoveCommandsForDocument(forest) == 0u);
	history.CancelTransaction(context);
}

TEST_CASE("Vegetation document history preserves saved checkpoint merge boundary and unreachable saved state")
{
	const AshEditor::EditorCommandDocumentKey meadow{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};
	const AshEditor::EditorCommandDocumentKey forest{
		"vegetation-layer",
		"vegetation/forest.ashvegetationlayer"
	};
	AshEditor::EditorContext context{};

	SUBCASE("ordinary execution merges before a saved checkpoint")
	{
		AshEditor::UndoRedoService history{};
		int value = 0;
		REQUIRE(history.Execute(
			std::make_unique<MergeableCountingCommand>(value, meadow), context));
		REQUIRE(history.Execute(
			std::make_unique<MergeableCountingCommand>(value, meadow), context));
		CHECK(value == 2);
		REQUIRE(history.Undo(context));
		CHECK(value == 0);
		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());
	}

	SUBCASE("saved checkpoint prevents ordinary merge and a new record clears redo")
	{
		AshEditor::UndoRedoService history{};
		int value = 0;
		REQUIRE(history.Execute(
			std::make_unique<MergeableCountingCommand>(value, meadow), context));
		history.MarkSaved();
		REQUIRE(history.Execute(
			std::make_unique<MergeableCountingCommand>(value, meadow), context));
		REQUIRE(history.Undo(context));
		CHECK(value == 1);
		CHECK(history.CanUndo());
		CHECK(history.CanRedo());
		CHECK_FALSE(history.IsDirty());

		int forest_value = 1;
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(forest_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		CHECK_FALSE(history.CanRedo());
		CHECK(history.IsDirty());
	}

	SUBCASE("saved state on an abandoned branch remains dirty after selective removal")
	{
		AshEditor::UndoRedoService history{};
		int meadow_value = 1;
		int forest_value = 1;
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(meadow_value, meadow), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(forest_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		history.MarkSaved();
		REQUIRE(history.Undo(context));
		CHECK(history.IsDirty());

		forest_value = 1;
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(forest_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		CHECK(history.IsDirty());
		CHECK(history.RemoveCommandsForDocument(meadow) == 1u);
		CHECK(history.IsDirty());
	}

	SUBCASE("surviving redo entries keep chronological state ids across document removal")
	{
		AshEditor::UndoRedoService history{};
		int first_value = 1;
		int removed_before_value = 1;
		int second_value = 1;
		int third_value = 1;
		int removed_after_value = 1;
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(first_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(removed_before_value, meadow), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(second_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(third_value, forest), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		history.MarkSaved();
		REQUIRE(history.RecordExecutedCommand(
			std::make_unique<CountingDocumentCommand>(removed_after_value, meadow), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);

		REQUIRE(history.Undo(context));
		REQUIRE(history.Undo(context));
		REQUIRE(history.Undo(context));
		REQUIRE(history.Undo(context));
		CHECK(first_value == 1);
		CHECK(second_value == 0);
		CHECK(third_value == 0);
		CHECK(history.IsDirty());

		CHECK(history.RemoveCommandsForDocument(meadow) == 2u);
		CHECK(history.CanRedo());
		CHECK(history.IsDirty());
		REQUIRE(history.Redo(context));
		CHECK(second_value == 1);
		CHECK(third_value == 0);
		CHECK(history.IsDirty());
		REQUIRE(history.Redo(context));
		CHECK(third_value == 1);
		CHECK_FALSE(history.IsDirty());
		CHECK_FALSE(history.CanRedo());
	}
}

TEST_CASE("Vegetation stroke command alternates canonical payload and advances its generation cursor")
{
	const auto initial = std::make_shared<const AshEngine::VegetationLayerSnapshot>(
		MakeCommandLayerSnapshot());
	REQUIRE(initial->tiles.size() == 1u);
	REQUIRE(initial->tiles[0].planes.size() == 2u);
	CHECK(initial->tiles[0].tile_x == -2);
	CHECK(initial->tiles[0].tile_z == 3);
	CHECK(initial->tiles[0].planes[0].values[0] == 255u);
	const AshEngine::VegetationBrushStroke stroke = MakeSingleTexelEraseStroke();
	REQUIRE(stroke.path.size() == 1u);
	AshEngine::VegetationWorldMillimeterPoint point{};
	REQUIRE(AshEngine::vegetation_surface_request_to_world_millimeter(
		stroke.path[0], point));
	CHECK(point.x == -63500);
	CHECK(point.z == 96500);
	CHECK(AshEngine::vegetation_brush_amount(
		0, stroke.radius_mm, stroke.strength, stroke.falloff) == 1u);
	auto working = std::make_shared<AshEngine::VegetationLayerWorkingSet>(initial);
	const std::vector<uint8_t> before_payload =
		VegetationTest::CanonicalAuthoringPayloadBytes(*initial);
	const AshEngine::VegetationMutationResult applied =
		AshEngine::apply_vegetation_brush_stroke(*working, stroke);
	REQUIRE(applied.status == AshEngine::VegetationMutationStatus::Applied);
	REQUIRE(applied.new_generation == 2u);
	const std::vector<uint8_t> after_payload =
		VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot());
	REQUIRE(after_payload != before_payload);

	const AshEditor::EditorCommandDocumentKey key{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};
	auto command = std::make_unique<AshEditor::VegetationStrokeCommand>(
		key,
		working,
		applied.patch,
		applied.new_generation);
	AshEditor::VegetationStrokeCommand* command_view = command.get();
	AshEditor::UndoRedoService history{};
	AshEditor::EditorContext context{};
	REQUIRE(history.RecordExecutedCommand(std::move(command), context) ==
		AshEditor::EditorCommandRecordResult::Recorded);
	CHECK(command_view->GetExpectedCurrentGeneration() == 2u);

	REQUIRE(history.Undo(context));
	CHECK(working->content_generation() == 3u);
	CHECK(command_view->GetExpectedCurrentGeneration() == 3u);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
		before_payload);
	REQUIRE(history.Redo(context));
	CHECK(working->content_generation() == 4u);
	CHECK(command_view->GetExpectedCurrentGeneration() == 4u);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
		after_payload);
	REQUIRE(history.Undo(context));
	CHECK(working->content_generation() == 5u);
	CHECK(command_view->GetExpectedCurrentGeneration() == 5u);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
		before_payload);
	REQUIRE(history.Redo(context));
	CHECK(working->content_generation() == 6u);
	CHECK(command_view->GetExpectedCurrentGeneration() == 6u);
	CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
		after_payload);
}

TEST_CASE("Vegetation stroke command stale Undo and Redo fail without moving history")
{
	const AshEditor::EditorCommandDocumentKey key{
		"vegetation-layer",
		"vegetation/meadow.ashvegetationlayer"
	};
	AshEditor::EditorContext context{};

	SUBCASE("stale Undo")
	{
		auto working = std::make_shared<AshEngine::VegetationLayerWorkingSet>(
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(
				MakeCommandLayerSnapshot()));
		const AshEngine::VegetationMutationResult applied =
			AshEngine::apply_vegetation_brush_stroke(*working, MakeSingleTexelEraseStroke());
		REQUIRE(applied.status == AshEngine::VegetationMutationStatus::Applied);
		auto command = std::make_unique<AshEditor::VegetationStrokeCommand>(
			key, working, applied.patch, applied.new_generation);
		AshEditor::VegetationStrokeCommand* command_view = command.get();
		AshEditor::UndoRedoService history{};
		REQUIRE(history.RecordExecutedCommand(std::move(command), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		const AshEngine::VegetationMutationResult intervening =
			AshEngine::apply_vegetation_palette_edit(*working, MakePaletteAdd());
		REQUIRE(intervening.status == AshEngine::VegetationMutationStatus::Applied);
		const uint64_t generation = working->content_generation();
		const std::vector<uint8_t> payload =
			VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot());

		CHECK_FALSE(history.Undo(context));
		CHECK(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK(working->content_generation() == generation);
		CHECK(command_view->GetExpectedCurrentGeneration() == applied.new_generation);
		CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
			payload);
	}

	SUBCASE("expired working set")
	{
		auto working = std::make_shared<AshEngine::VegetationLayerWorkingSet>(
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(
				MakeCommandLayerSnapshot()));
		const AshEngine::VegetationMutationResult applied =
			AshEngine::apply_vegetation_brush_stroke(*working, MakeSingleTexelEraseStroke());
		REQUIRE(applied.status == AshEngine::VegetationMutationStatus::Applied);
		auto command = std::make_unique<AshEditor::VegetationStrokeCommand>(
			key, working, applied.patch, applied.new_generation);
		AshEditor::VegetationStrokeCommand* command_view = command.get();
		const uint64_t expected_cursor = command_view->GetExpectedCurrentGeneration();
		working.reset();

		AshEditor::UndoRedoService history{};
		REQUIRE(history.RecordExecutedCommand(std::move(command), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		CHECK_FALSE(history.Undo(context));
		CHECK(history.CanUndo());
		CHECK_FALSE(history.CanRedo());
		CHECK(command_view->GetExpectedCurrentGeneration() == expected_cursor);
	}

	SUBCASE("stale Redo")
	{
		auto working = std::make_shared<AshEngine::VegetationLayerWorkingSet>(
			std::make_shared<const AshEngine::VegetationLayerSnapshot>(
				MakeCommandLayerSnapshot()));
		const AshEngine::VegetationMutationResult applied =
			AshEngine::apply_vegetation_brush_stroke(*working, MakeSingleTexelEraseStroke());
		REQUIRE(applied.status == AshEngine::VegetationMutationStatus::Applied);
		auto command = std::make_unique<AshEditor::VegetationStrokeCommand>(
			key, working, applied.patch, applied.new_generation);
		AshEditor::VegetationStrokeCommand* command_view = command.get();
		AshEditor::UndoRedoService history{};
		REQUIRE(history.RecordExecutedCommand(std::move(command), context) ==
			AshEditor::EditorCommandRecordResult::Recorded);
		REQUIRE(history.Undo(context));
		const uint64_t expected_cursor = command_view->GetExpectedCurrentGeneration();
		const AshEngine::VegetationMutationResult intervening =
			AshEngine::apply_vegetation_palette_edit(*working, MakePaletteAdd());
		REQUIRE(intervening.status == AshEngine::VegetationMutationStatus::Applied);
		const uint64_t generation = working->content_generation();
		const std::vector<uint8_t> payload =
			VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot());

		CHECK_FALSE(history.Redo(context));
		CHECK_FALSE(history.CanUndo());
		CHECK(history.CanRedo());
		CHECK(working->content_generation() == generation);
		CHECK(command_view->GetExpectedCurrentGeneration() == expected_cursor);
		CHECK(VegetationTest::CanonicalAuthoringPayloadBytes(*working->publish_snapshot()) ==
			payload);
	}
}
