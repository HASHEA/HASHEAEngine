#include "doctest.h"

#include "Core/EditorCommand.h"
#include "Core/EditorIds.h"
#include "Core/IEditorCommandExecutor.h"
#include "Panels/Vegetation/VegetationPanel.h"
#include "Services/VegetationEditorService.h"
#include "Vegetation/VegetationTestSupport.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace
{
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
			++executed_count;
			return true;
		}

		AshEditor::EditorCommandRecordResult RecordExecutedCommand(
			std::unique_ptr<AshEditor::EditorCommand> command) override
		{
			if (!command)
			{
				return AshEditor::EditorCommandRecordResult::RollbackFailed;
			}
			++recorded_count;
			return AshEditor::EditorCommandRecordResult::Recorded;
		}

		std::size_t RemoveCommandsForDocument(
			const AshEditor::EditorCommandDocumentKey&) override
		{
			return 0;
		}

		size_t executed_count = 0;
		size_t recorded_count = 0;
	};

	AshEditor::VegetationEditorServiceDeps MakeServiceDeps(
		AshEngine::AssetDatabase& database,
		const VegetationTest::ScopedAssetRoot& root,
		RecordingCommandExecutor& commands,
		VegetationTest::ManualVegetationEditorTaskExecutor& executor)
	{
		AshEditor::VegetationEditorServiceDeps deps{};
		deps.pAssetDatabase = &database;
		deps.asset_root = root.Path();
		deps.pCommandExecutor = &commands;
		deps.pSurfaceProvider = nullptr;
		deps.pTaskExecutor = &executor;
		deps.load_budget =
			AshEditor::VegetationEditorService::DefaultLoadBudget();
		deps.chunk_set_load_budget =
			AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
		deps.create_layer_id = []
		{
			return VegetationTest::SequentialId(0x41);
		};
		return deps;
	}

	size_t CountOccurrences(
		const std::string& text,
		const std::string& needle)
	{
		size_t count = 0;
		size_t position = 0;
		while ((position = text.find(needle, position)) !=
			std::string::npos)
		{
			++count;
			position += needle.size();
		}
		return count;
	}
}

TEST_CASE("Vegetation panel exposes a disabled-safe no-provider contract")
{
	VegetationTest::ScopedAssetRoot root("panel-no-provider");
	AshEngine::AssetDatabase database =
		AshEngine::AssetDatabase::create(root.Path());
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	RecordingCommandExecutor commands{};
	AshEditor::VegetationEditorService service(
		MakeServiceDeps(database, root, commands, executor));
	REQUIRE(service.Initialize());

	AshEditor::VegetationPanel panel({ &service });
	CHECK(panel.GetId() == AshEditor::EditorPanelIds::Vegetation);
	CHECK(panel.GetTitle() == AshEditor::EditorWindowTitles::Vegetation);

	const auto default_budget =
		AshEditor::VegetationEditorService::DefaultLoadBudget();
	const auto default_chunk_budget =
		AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
	CHECK(default_budget.max_file_bytes > 0);
	CHECK(default_budget.max_decoded_bytes > 0);
	CHECK(default_chunk_budget.per_file.max_file_bytes > 0);
	CHECK(default_chunk_budget.max_total_inspected_bytes > 0);

	const AshEditor::VegetationEditorStatusSnapshot status =
		service.GetStatusSnapshot();
	CHECK(status.capabilities.can_load);
	REQUIRE(status.palette != nullptr);
	CHECK_FALSE(status.capabilities.can_paint);
	CHECK_FALSE(status.capabilities.can_erase);
	CHECK_FALSE(status.capabilities.can_bake);
	CHECK(status.capabilities.surface_unavailable_reason ==
		"No vegetation surface provider is registered.");
}

TEST_CASE("Vegetation panel cache follows attach update and detach lifecycle")
{
	VegetationTest::ScopedAssetRoot root("panel-update");
	AshEngine::AssetDatabase database =
		AshEngine::AssetDatabase::create(root.Path());
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	RecordingCommandExecutor commands{};
	AshEditor::VegetationEditorService service(
		MakeServiceDeps(database, root, commands, executor));
	REQUIRE(service.Initialize());
	AshEditor::VegetationPanel panel({ &service });

	const AshEditor::VegetationEditorStatusSnapshot beforeAttach =
		panel.GetCachedStatus();
	CHECK(beforeAttach.session ==
		AshEditor::VegetationSessionState::Failed);
	CHECK(beforeAttach.source_path.empty());
	CHECK(beforeAttach.content_generation == 0);
	CHECK_FALSE(beforeAttach.capabilities.can_load);

	panel.OnAttach();
	const AshEditor::VegetationEditorStatusSnapshot afterAttach =
		panel.GetCachedStatus();
	CHECK(afterAttach.capabilities.can_load);
	CHECK_FALSE(afterAttach.capabilities.can_paint);
	CHECK(afterAttach.capabilities.surface_unavailable_reason ==
		"No vegetation surface provider is registered.");

	REQUIRE(service.CreateLayer(
		"panel-cache.AshVegetationLayer", 0x1234u));
	const AshEditor::VegetationEditorStatusSnapshot serviceAfterCreate =
		service.GetStatusSnapshot();
	REQUIRE(serviceAfterCreate.session ==
		AshEditor::VegetationSessionState::Dirty);
	REQUIRE(serviceAfterCreate.source_path ==
		"panel-cache.AshVegetationLayer");
	REQUIRE(serviceAfterCreate.content_generation == 1);

	CHECK(panel.GetCachedStatus().source_path ==
		afterAttach.source_path);
	CHECK(panel.GetCachedStatus().content_generation ==
		afterAttach.content_generation);
	CHECK(panel.GetCachedStatus().session ==
		afterAttach.session);

	panel.OnUpdate();
	const AshEditor::VegetationEditorStatusSnapshot afterUpdate =
		panel.GetCachedStatus();
	CHECK(afterUpdate.session == serviceAfterCreate.session);
	CHECK(afterUpdate.source_path == serviceAfterCreate.source_path);
	CHECK(afterUpdate.content_generation ==
		serviceAfterCreate.content_generation);
	CHECK(afterUpdate.capabilities.can_load ==
		serviceAfterCreate.capabilities.can_load);
	CHECK(afterUpdate.capabilities.surface_unavailable_reason ==
		serviceAfterCreate.capabilities.surface_unavailable_reason);

	panel.OnDetach();
	const AshEditor::VegetationEditorStatusSnapshot afterDetach =
		panel.GetCachedStatus();
	CHECK(afterDetach.session ==
		AshEditor::VegetationSessionState::Failed);
	CHECK(afterDetach.source_path.empty());
	CHECK(afterDetach.content_generation == 0);
	CHECK_FALSE(afterDetach.capabilities.can_load);

	service.Shutdown();
	CHECK_NOTHROW(panel.OnUpdate());
	CHECK(panel.GetCachedStatus().source_path.empty());
	CHECK_FALSE(panel.GetCachedStatus().capabilities.can_load);
	CHECK(commands.executed_count == 0);
	CHECK(commands.recorded_count == 0);
}

TEST_CASE("Vegetation panel detach makes later updates service-lifetime safe")
{
	VegetationTest::ScopedAssetRoot root("panel-detach");
	AshEngine::AssetDatabase database =
		AshEngine::AssetDatabase::create(root.Path());
	VegetationTest::ManualVegetationEditorTaskExecutor executor{};
	RecordingCommandExecutor commands{};
	AshEditor::VegetationEditorService service(
		MakeServiceDeps(database, root, commands, executor));
	REQUIRE(service.Initialize());
	AshEditor::VegetationPanel panel({ &service });

	panel.OnAttach();
	panel.OnDetach();
	service.Shutdown();
	panel.OnUpdate();
	panel.OnDetach();

	CHECK(executor.IsIdle());
	CHECK(commands.executed_count == 0);
	CHECK(commands.recorded_count == 0);
}

TEST_CASE("Vegetation panel source advertises only the canonical Species extension")
{
	const std::filesystem::path sourcePath =
		std::filesystem::path(__FILE__).parent_path() /
		"../../editor/Panels/Vegetation/VegetationPanel.cpp";
	std::ifstream input(sourcePath, std::ios::binary);
	REQUIRE(input.is_open());
	const std::string source{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()
	};

	CHECK(source.find(".AshVegetationSpecies") ==
		std::string::npos);
	CHECK(CountOccurrences(
		source,
		".AshVegetation asset.") == 2);
}
