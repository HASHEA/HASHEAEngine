#include "Core/EditorContext.h"
#include "Core/EntityCommands.h"
#include "Core/EditorEventBindings.h"
#include "Core/EditorEventBus.h"
#include "Core/EditorEvents.h"
#include "Core/SceneSnapshotUtils.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelEvents.h"
#include "Panels/SceneHierarchy/SceneHierarchyPanelState.h"
#include "Services/SceneService.h"
#include "Services/SelectionService.h"
#include "Services/UndoRedoService.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	struct CommandHarness
	{
		AshEditor::SceneService scene_service{};
		AshEditor::SelectionService selection_service{};
		AshEditor::UndoRedoService history{};
		AshEditor::EditorContext context{};

		bool initialize()
		{
			if (!scene_service.Initialize(std::filesystem::path{}))
			{
				return false;
			}
			context.pSceneService = &scene_service;
			context.pSelectionService = &selection_service;
			return true;
		}
	};

	struct VegetationRootPair
	{
		AshEngine::Entity surface{};
		AshEngine::Entity vegetation{};
	};

	VegetationRootPair CreateVegetationRootPair(AshEditor::SceneService& scene_service)
	{
		VegetationRootPair pair{};
		pair.surface = scene_service.CreateEntity("Surface Root");
		pair.vegetation = scene_service.CreateEntity("Vegetation Root");
		AshEngine::VegetationComponent component{};
		component.layer_asset_path = "vegetation/meadow.AshVegetationLayer";
		component.surface_entity_id = pair.surface.get_id();
		component.enabled = true;
		if (!pair.surface.is_valid() || !pair.vegetation.is_valid() ||
			!pair.vegetation.add_vegetation_component(component))
		{
			return {};
		}
		return pair;
	}

	std::vector<AshEngine::EntityId> GetSelectedEntityIds(
		const AshEditor::SelectionService& selection_service)
	{
		return selection_service.GetSelectedIds(AshEditor::EditorSelectionKind::Entity);
	}

	bool ContainsEntityId(
		const std::vector<AshEngine::EntityId>& ids,
		const AshEngine::EntityId id)
	{
		return std::find(ids.begin(), ids.end(), id) != ids.end();
	}

	std::vector<AshEngine::EntityId> CollectRootOrder(
		const AshEngine::Scene& scene,
		const std::vector<AshEngine::EntityId>& included_ids)
	{
		std::vector<AshEngine::EntityId> order{};
		for (const AshEngine::Entity& root : scene.get_root_entities())
		{
			if (ContainsEntityId(included_ids, root.get_id()))
			{
				order.push_back(root.get_id());
			}
		}
		return order;
	}

	std::pair<AshEngine::EntityId, AshEngine::EntityId> ResolveCopiedVegetationAndSurface(
		const AshEngine::Scene& scene,
		const std::vector<AshEngine::EntityId>& copied_ids)
	{
		AshEngine::EntityId copied_vegetation_id = 0;
		AshEngine::EntityId copied_surface_id = 0;
		for (const AshEngine::EntityId id : copied_ids)
		{
			const AshEngine::Entity entity = scene.find_entity(id);
			if (!entity.is_valid())
			{
				return {};
			}
			if (entity.has_vegetation_component())
			{
				copied_vegetation_id = id;
			}
			else
			{
				copied_surface_id = id;
			}
		}
		return { copied_vegetation_id, copied_surface_id };
	}
}

TEST_CASE("VegetationComponent DuplicateEntitiesCommand Execute Undo Redo preserves forest remap and sibling order")
{
	for (const bool reverse_selection_order : { false, true })
	{
		CAPTURE(reverse_selection_order);
		CommandHarness harness{};
		REQUIRE(harness.initialize());
		const AshEngine::Entity prefix = harness.scene_service.CreateEntity("Prefix");
		VegetationRootPair pair = CreateVegetationRootPair(harness.scene_service);
		const AshEngine::Entity middle = harness.scene_service.CreateEntity("Middle");
		const AshEngine::Entity suffix = harness.scene_service.CreateEntity("Suffix");
		REQUIRE(prefix.is_valid());
		REQUIRE(pair.surface.is_valid());
		REQUIRE(pair.vegetation.is_valid());
		REQUIRE(middle.is_valid());
		REQUIRE(suffix.is_valid());
		REQUIRE(harness.scene_service.ReparentEntity(
			middle.get_id(),
			0,
			harness.scene_service.GetEntitySiblingIndex(pair.vegetation.get_id())));
		const std::vector<AshEngine::EntityId> initial_order = {
			prefix.get_id(),
			pair.surface.get_id(),
			middle.get_id(),
			pair.vegetation.get_id(),
			suffix.get_id(),
		};
		CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), initial_order) == initial_order);

		const std::vector<AshEngine::EntityId> source_ids = reverse_selection_order
			? std::vector<AshEngine::EntityId>{ pair.vegetation.get_id(), pair.surface.get_id() }
			: std::vector<AshEngine::EntityId>{ pair.surface.get_id(), pair.vegetation.get_id() };
		REQUIRE(harness.history.Execute(
			std::make_unique<AshEditor::DuplicateEntitiesCommand>(source_ids),
			harness.context));

		const std::vector<AshEngine::EntityId> copied_ids =
			GetSelectedEntityIds(harness.selection_service);
		REQUIRE(copied_ids.size() == 2u);
		const auto [copied_vegetation_id, copied_surface_id] =
			ResolveCopiedVegetationAndSurface(harness.scene_service.GetActiveScene(), copied_ids);
		REQUIRE(copied_vegetation_id != 0u);
		REQUIRE(copied_surface_id != 0u);
		CHECK(copied_ids == (reverse_selection_order
			? std::vector<AshEngine::EntityId>{ copied_vegetation_id, copied_surface_id }
			: std::vector<AshEngine::EntityId>{ copied_surface_id, copied_vegetation_id }));
		CHECK(copied_vegetation_id != pair.vegetation.get_id());
		CHECK(copied_surface_id != pair.surface.get_id());
		CHECK(harness.scene_service.FindEntity(copied_vegetation_id)
			.get_vegetation_component().surface_entity_id == copied_surface_id);

		const std::vector<AshEngine::EntityId> involved_ids = {
			prefix.get_id(),
			pair.surface.get_id(),
			copied_surface_id,
			middle.get_id(),
			pair.vegetation.get_id(),
			copied_vegetation_id,
			suffix.get_id(),
		};
		std::vector<AshEngine::EntityId> pair_order =
			CollectRootOrder(harness.scene_service.GetActiveScene(), involved_ids);
		CHECK(pair_order == std::vector<AshEngine::EntityId>{
			prefix.get_id(), pair.surface.get_id(), copied_surface_id, middle.get_id(),
			pair.vegetation.get_id(), copied_vegetation_id, suffix.get_id() });

		REQUIRE(harness.history.Undo(harness.context));
		CHECK_FALSE(harness.scene_service.FindEntity(copied_vegetation_id).is_valid());
		CHECK_FALSE(harness.scene_service.FindEntity(copied_surface_id).is_valid());
		CHECK(GetSelectedEntityIds(harness.selection_service) == source_ids);
		CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), initial_order) == initial_order);

		REQUIRE(harness.history.Redo(harness.context));
		CHECK(GetSelectedEntityIds(harness.selection_service) == copied_ids);
		REQUIRE(harness.scene_service.FindEntity(copied_vegetation_id).is_valid());
		REQUIRE(harness.scene_service.FindEntity(copied_surface_id).is_valid());
		CHECK(harness.scene_service.FindEntity(copied_vegetation_id)
			.get_vegetation_component().surface_entity_id == copied_surface_id);
		CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), involved_ids) ==
			std::vector<AshEngine::EntityId>{
				prefix.get_id(), pair.surface.get_id(), copied_surface_id, middle.get_id(),
				pair.vegetation.get_id(), copied_vegetation_id, suffix.get_id() });
	}
}

TEST_CASE("VegetationComponent PasteEntitySnapshotsCommand Execute Undo Redo preserves cross root references")
{
	CommandHarness harness{};
	REQUIRE(harness.initialize());
	VegetationRootPair pair = CreateVegetationRootPair(harness.scene_service);
	REQUIRE(pair.surface.is_valid());
	REQUIRE(pair.vegetation.is_valid());

	const std::optional<AshEditor::SceneEntitySnapshot> vegetation_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(
			harness.scene_service.GetActiveScene(), pair.vegetation.get_id());
	const std::optional<AshEditor::SceneEntitySnapshot> surface_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(
			harness.scene_service.GetActiveScene(), pair.surface.get_id());
	REQUIRE(vegetation_snapshot.has_value());
	REQUIRE(surface_snapshot.has_value());

	std::vector<AshEditor::SceneEntitySnapshot> snapshots{
		*vegetation_snapshot,
		*surface_snapshot,
	};
	REQUIRE(harness.history.Execute(
		std::make_unique<AshEditor::PasteEntitySnapshotsCommand>(
			std::move(snapshots),
			std::vector<AshEngine::EntityId>{ 0u, 0u }),
		harness.context));

	const std::vector<AshEngine::EntityId> copied_ids = GetSelectedEntityIds(harness.selection_service);
	REQUIRE(copied_ids.size() == 2u);
	const auto [copied_vegetation_id, copied_surface_id] =
		ResolveCopiedVegetationAndSurface(harness.scene_service.GetActiveScene(), copied_ids);
	REQUIRE(copied_vegetation_id != 0u);
	REQUIRE(copied_surface_id != 0u);
	CHECK(copied_ids == std::vector<AshEngine::EntityId>{ copied_vegetation_id, copied_surface_id });
	CHECK(harness.scene_service.FindEntity(copied_vegetation_id)
		.get_vegetation_component().surface_entity_id == copied_surface_id);
	const std::vector<AshEngine::EntityId> paste_order = {
		pair.surface.get_id(), pair.vegetation.get_id(), copied_vegetation_id, copied_surface_id };
	CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), paste_order) == paste_order);

	REQUIRE(harness.history.Undo(harness.context));
	CHECK(GetSelectedEntityIds(harness.selection_service).empty());
	CHECK_FALSE(harness.scene_service.FindEntity(copied_vegetation_id).is_valid());
	CHECK_FALSE(harness.scene_service.FindEntity(copied_surface_id).is_valid());

	REQUIRE(harness.history.Redo(harness.context));
	CHECK(GetSelectedEntityIds(harness.selection_service) == copied_ids);
	REQUIRE(harness.scene_service.FindEntity(copied_vegetation_id).is_valid());
	REQUIRE(harness.scene_service.FindEntity(copied_surface_id).is_valid());
	CHECK(harness.scene_service.FindEntity(copied_vegetation_id)
		.get_vegetation_component().surface_entity_id == copied_surface_id);
	CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), paste_order) == paste_order);
}

TEST_CASE("Vegetation scene hierarchy reset clears the scene-local snapshot clipboard")
{
	AshEditor::SceneHierarchyPanelState state{};
	AshEditor::EditorEventBus event_bus{};
	AshEditor::EditorEventBindings event_bindings{};
	event_bindings.Bind(&event_bus);
	AshEditor::BindSceneHierarchySceneLifetimeEvents(event_bindings, state);

	const auto seed_clipboard = [&state]()
		{
			AshEditor::SceneEntitySnapshot snapshot{};
			snapshot.uEntityId = 42;
			state.vecClipboardEntitySnapshots.push_back(snapshot);
			state.vecClipboardPreferredParentEntityIds.push_back(7);
		};

	seed_clipboard();
	event_bus.Publish(AshEditor::EditorActiveSceneChangedEvent{});
	CHECK(state.vecClipboardEntitySnapshots.empty());
	CHECK(state.vecClipboardPreferredParentEntityIds.empty());

	seed_clipboard();
	AshEditor::EditorSceneChangedEvent reload_event{};
	reload_event.eKind = AshEngine::SceneChangeKind::SceneReloaded;
	event_bus.Publish(reload_event);
	CHECK(state.vecClipboardEntitySnapshots.empty());
	CHECK(state.vecClipboardPreferredParentEntityIds.empty());

	seed_clipboard();
	AshEditor::EditorSceneChangedEvent replace_event{};
	replace_event.eKind = AshEngine::SceneChangeKind::SceneReplaced;
	event_bus.Publish(replace_event);
	CHECK(state.vecClipboardEntitySnapshots.empty());
	CHECK(state.vecClipboardPreferredParentEntityIds.empty());
}

TEST_CASE("VegetationComponent DeleteEntitiesCommand Execute Undo Redo restores the complete reference pair")
{
	CommandHarness harness{};
	REQUIRE(harness.initialize());
	const AshEngine::Entity prefix = harness.scene_service.CreateEntity("Prefix");
	const AshEngine::Entity surface = harness.scene_service.CreateEntity("Surface Root");
	const AshEngine::Entity middle = harness.scene_service.CreateEntity("Middle");
	AshEngine::Entity vegetation = harness.scene_service.CreateEntity("Vegetation Root");
	const AshEngine::Entity suffix = harness.scene_service.CreateEntity("Suffix");
	REQUIRE(prefix.is_valid());
	REQUIRE(surface.is_valid());
	REQUIRE(middle.is_valid());
	REQUIRE(vegetation.is_valid());
	REQUIRE(suffix.is_valid());
	AshEngine::VegetationComponent component{};
	component.layer_asset_path = "vegetation/meadow.AshVegetationLayer";
	component.surface_entity_id = surface.get_id();
	component.enabled = true;
	REQUIRE(vegetation.add_vegetation_component(component));
	const AshEngine::EntityId surface_id = surface.get_id();
	const AshEngine::EntityId vegetation_id = vegetation.get_id();
	const std::vector<AshEngine::EntityId> initial_order = {
		prefix.get_id(), surface_id, middle.get_id(), vegetation_id, suffix.get_id() };
	CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), initial_order) == initial_order);

	REQUIRE(harness.history.Execute(
		std::make_unique<AshEditor::DeleteEntitiesCommand>(
			std::vector<AshEngine::EntityId>{ vegetation_id, surface_id }),
		harness.context));
	CHECK_FALSE(harness.scene_service.FindEntity(surface_id).is_valid());
	CHECK_FALSE(harness.scene_service.FindEntity(vegetation_id).is_valid());
	CHECK(GetSelectedEntityIds(harness.selection_service).empty());

	REQUIRE(harness.history.Undo(harness.context));
	REQUIRE(harness.scene_service.FindEntity(surface_id).is_valid());
	REQUIRE(harness.scene_service.FindEntity(vegetation_id).is_valid());
	CHECK(harness.scene_service.FindEntity(vegetation_id)
		.get_vegetation_component().surface_entity_id == surface_id);
	CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), initial_order) == initial_order);
	CHECK(GetSelectedEntityIds(harness.selection_service) ==
		std::vector<AshEngine::EntityId>{ vegetation_id, surface_id });

	REQUIRE(harness.history.Redo(harness.context));
	CHECK_FALSE(harness.scene_service.FindEntity(surface_id).is_valid());
	CHECK_FALSE(harness.scene_service.FindEntity(vegetation_id).is_valid());
	CHECK(GetSelectedEntityIds(harness.selection_service).empty());
	CHECK(CollectRootOrder(harness.scene_service.GetActiveScene(), initial_order) ==
		std::vector<AshEngine::EntityId>{ prefix.get_id(), middle.get_id(), suffix.get_id() });
}
