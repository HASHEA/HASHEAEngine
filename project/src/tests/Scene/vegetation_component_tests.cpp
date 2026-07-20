#include "Core/SceneComponentSerialization.h"
#include "Core/SceneSnapshotUtils.h"
#include "Function/Scene/Scene.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	const AshEngine::ScenePropertyDesc* FindProperty(
		const AshEngine::SceneComponentDesc& descriptor,
		const std::string& name)
	{
		for (uint32_t property_index = 0; property_index < descriptor.property_count; ++property_index)
		{
			const AshEngine::ScenePropertyDesc& property = descriptor.properties[property_index];
			if (property.name && name == property.name)
			{
				return &property;
			}
		}
		return nullptr;
	}

	void WriteJson(const fs::path& path, const json& value)
	{
		std::ofstream output(path);
		REQUIRE(output.is_open());
		output << value.dump(2);
		REQUIRE(output.good());
	}

	json ReadJson(const fs::path& path)
	{
		std::ifstream input(path);
		REQUIRE(input.is_open());
		json value{};
		input >> value;
		return value;
	}

	AshEngine::VegetationComponent MakeVegetation(
		const AshEngine::EntityId surface_entity_id,
		std::string layer_asset_path = "vegetation/meadow.AshVegetationLayer")
	{
		AshEngine::VegetationComponent component{};
		component.layer_asset_path = std::move(layer_asset_path);
		component.surface_entity_id = surface_entity_id;
		component.enabled = true;
		return component;
	}

	AshEditor::SceneEntitySnapshot* FindSnapshotEntity(
		AshEditor::SceneEntitySnapshot& snapshot,
		const AshEngine::EntityId entity_id)
	{
		if (snapshot.uEntityId == entity_id)
		{
			return &snapshot;
		}
		for (AshEditor::SceneEntitySnapshot& child : snapshot.vecChildren)
		{
			if (AshEditor::SceneEntitySnapshot* found = FindSnapshotEntity(child, entity_id))
			{
				return found;
			}
		}
		return nullptr;
	}

	AshEditor::SceneComponentSnapshot* FindSnapshotComponent(
		AshEditor::SceneEntitySnapshot& snapshot,
		const AshEngine::SceneComponentType type)
	{
		for (AshEditor::SceneComponentSnapshot& component : snapshot.vecComponents)
		{
			if (component.eType == type)
			{
				return &component;
			}
		}
		return nullptr;
	}
}

TEST_CASE("VegetationComponent descriptor facade extraction and independent version are stable")
{
	const AshEngine::SceneComponentDesc* descriptor =
		AshEngine::get_scene_component_descriptor(AshEngine::SceneComponentType::Vegetation);
	REQUIRE(descriptor != nullptr);
	CHECK(std::string(descriptor->name) == "VegetationComponent");
	CHECK(descriptor->byte_size == sizeof(AshEngine::VegetationComponent));
	CHECK(descriptor->property_count == 3u);

	const AshEngine::ScenePropertyDesc* layer_path = FindProperty(*descriptor, "layer_asset_path");
	REQUIRE(layer_path != nullptr);
	CHECK(layer_path->type == AshEngine::ScenePropertyType::String);
	CHECK(layer_path->editor_hint == AshEngine::ScenePropertyEditorHint::AssetPath);
	CHECK(layer_path->asset_ref_kind == AshEngine::ScenePropertyAssetRefKind::VegetationLayer);
	const AshEngine::ScenePropertyDesc* surface_id = FindProperty(*descriptor, "surface_entity_id");
	REQUIRE(surface_id != nullptr);
	CHECK(surface_id->type == AshEngine::ScenePropertyType::UInt64);
	const AshEngine::ScenePropertyDesc* enabled = FindProperty(*descriptor, "enabled");
	REQUIRE(enabled != nullptr);
	CHECK(enabled->type == AshEngine::ScenePropertyType::Bool);

	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Scene");
	const AshEngine::EntityId high_surface_id = (1ull << 60u) + 17u;
	AshEngine::Entity surface = scene.create_entity_with_id(high_surface_id, "Surface");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation");
	REQUIRE(surface.is_valid());
	REQUIRE(vegetation.is_valid());

	const uint64_t primitive_version = scene.get_render_primitive_version();
	const uint64_t transform_version = scene.get_render_transform_version();
	const uint64_t light_version = scene.get_render_light_version();
	const uint64_t environment_version = scene.get_render_environment_version();
	const uint64_t particle_version = scene.get_render_particle_version();
	const uint64_t config_version = scene.get_render_config_version();
	const uint64_t vegetation_version = scene.get_vegetation_version();

	CHECK_FALSE(AshEngine::can_add_scene_component(vegetation, AshEngine::SceneComponentType::Vegetation));
	CHECK_FALSE(AshEngine::add_scene_component(vegetation, AshEngine::SceneComponentType::Vegetation));

	const AshEngine::VegetationComponent expected = MakeVegetation(surface.get_id());
	REQUIRE(vegetation.add_vegetation_component(expected));
	CHECK(scene.get_vegetation_version() > vegetation_version);
	CHECK(scene.get_render_primitive_version() == primitive_version);
	CHECK(scene.get_render_transform_version() == transform_version);
	CHECK(scene.get_render_light_version() == light_version);
	CHECK(scene.get_render_environment_version() == environment_version);
	CHECK(scene.get_render_particle_version() == particle_version);
	CHECK(scene.get_render_config_version() == config_version);

	CHECK(vegetation.has_vegetation_component());
	const AshEngine::VegetationComponent stored = vegetation.get_vegetation_component();
	CHECK(stored.layer_asset_path == expected.layer_asset_path);
	CHECK(stored.surface_entity_id == high_surface_id);
	CHECK(stored.enabled == expected.enabled);

	AshEngine::VegetationComponent raw_read{};
	REQUIRE(vegetation.read_component(
		AshEngine::SceneComponentType::Vegetation,
		&raw_read,
		sizeof(raw_read)));
	CHECK(raw_read.surface_entity_id == high_surface_id);

	const std::string payload =
		AshEditor::SceneComponentSerialization::SerializeComponentPayload(&raw_read, *descriptor);
	CHECK(json::parse(payload).at("surface_entity_id") == high_surface_id);
	AshEngine::VegetationComponent payload_roundtrip{};
	REQUIRE(AshEditor::SceneComponentSerialization::DeserializeComponentPayload(
		payload,
		*descriptor,
		&payload_roundtrip));
	CHECK(payload_roundtrip.surface_entity_id == high_surface_id);

	const std::vector<AshEngine::SceneVegetationExtractionDesc> extracted =
		scene.extract_vegetation_entities();
	REQUIRE(extracted.size() == 1u);
	CHECK(extracted[0].entity_id == vegetation.get_id());
	CHECK(extracted[0].vegetation.layer_asset_path == expected.layer_asset_path);
	CHECK(extracted[0].vegetation.surface_entity_id == high_surface_id);

	AshEngine::VegetationComponent disabled = expected;
	disabled.enabled = false;
	const uint64_t before_write = scene.get_vegetation_version();
	REQUIRE(vegetation.set_vegetation_component(disabled));
	CHECK(scene.get_vegetation_version() == before_write + 1u);
	CHECK_FALSE(vegetation.get_vegetation_component().enabled);
	CHECK(scene.get_render_primitive_version() == primitive_version);
	CHECK(scene.get_render_transform_version() == transform_version);
	CHECK(scene.get_render_light_version() == light_version);
	CHECK(scene.get_render_environment_version() == environment_version);
	CHECK(scene.get_render_particle_version() == particle_version);
	CHECK(scene.get_render_config_version() == config_version);
	AshEngine::VegetationComponent enabled_again = disabled;
	enabled_again.enabled = true;
	const uint64_t before_generic_write = scene.get_vegetation_version();
	REQUIRE(vegetation.write_component(
		AshEngine::SceneComponentType::Vegetation,
		&enabled_again,
		sizeof(enabled_again)));
	CHECK(scene.get_vegetation_version() == before_generic_write + 1u);
	CHECK(vegetation.get_vegetation_component().enabled);

	CHECK(AshEngine::can_remove_scene_component(vegetation, AshEngine::SceneComponentType::Vegetation));
	const uint64_t before_remove = scene.get_vegetation_version();
	REQUIRE(vegetation.remove_vegetation_component());
	CHECK_FALSE(vegetation.has_vegetation_component());
	CHECK(scene.get_vegetation_version() == before_remove + 1u);
	CHECK(scene.get_render_primitive_version() == primitive_version);
	CHECK(scene.get_render_transform_version() == transform_version);
	CHECK(scene.get_render_light_version() == light_version);
	CHECK(scene.get_render_environment_version() == environment_version);
	CHECK(scene.get_render_particle_version() == particle_version);
	CHECK(scene.get_render_config_version() == config_version);

	REQUIRE(vegetation.add_vegetation_component(expected));
	REQUIRE(AshEngine::remove_scene_component(vegetation, AshEngine::SceneComponentType::Vegetation));
	CHECK_FALSE(vegetation.has_vegetation_component());
	CHECK(scene.get_render_primitive_version() == primitive_version);
	CHECK(scene.get_render_transform_version() == transform_version);
	CHECK(scene.get_render_light_version() == light_version);
	CHECK(scene.get_render_environment_version() == environment_version);
	CHECK(scene.get_render_particle_version() == particle_version);
	CHECK(scene.get_render_config_version() == config_version);
}

TEST_CASE("VegetationComponent rejects invalid layer paths and surface references without mutation")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Validation");
	AshEngine::Entity surface = scene.create_entity("Surface");
	REQUIRE(surface.is_valid());

	const std::vector<std::string> invalid_paths = {
		"",
		"C:/vegetation/meadow.AshVegetationLayer",
		"/vegetation/meadow.AshVegetationLayer",
		"vegetation/../meadow.AshVegetationLayer",
		"./vegetation/meadow.AshVegetationLayer",
		"vegetation//meadow.AshVegetationLayer",
		"vegetation\\meadow.AshVegetationLayer",
		"vegetation/meadow.txt",
	};

	for (size_t index = 0; index < invalid_paths.size(); ++index)
	{
		AshEngine::Entity entity = scene.create_entity("Invalid Path");
		const uint64_t before = scene.get_vegetation_version();
		CHECK_FALSE(entity.add_vegetation_component(MakeVegetation(surface.get_id(), invalid_paths[index])));
		CHECK_FALSE(entity.has_vegetation_component());
		CHECK(scene.get_vegetation_version() == before);
	}

	AshEngine::Entity zero = scene.create_entity("Zero Surface");
	AshEngine::Entity missing = scene.create_entity("Missing Surface");
	AshEngine::Entity self = scene.create_entity("Self Surface");
	CHECK_FALSE(zero.add_vegetation_component(MakeVegetation(0)));
	CHECK_FALSE(missing.add_vegetation_component(MakeVegetation(0xfedcba9876543210ull)));
	CHECK_FALSE(self.add_vegetation_component(MakeVegetation(self.get_id())));

	AshEngine::Entity valid = scene.create_entity("Valid");
	REQUIRE(valid.add_vegetation_component(
		MakeVegetation(surface.get_id(), "vegetation/meadow.ashvegetationlayer")));
	AshEngine::VegetationComponent invalid_set = valid.get_vegetation_component();
	invalid_set.surface_entity_id = valid.get_id();
	const uint64_t before_set = scene.get_vegetation_version();
	CHECK_FALSE(valid.set_vegetation_component(invalid_set));
	CHECK(scene.get_vegetation_version() == before_set);
	CHECK(valid.get_vegetation_component().surface_entity_id == surface.get_id());
}

TEST_CASE("VegetationComponent scene v7 roundtrip is exact and legacy v3 through v6 stay absent")
{
	const fs::path temp_directory = fs::path("Intermediate") / "test-temp" / "scene";
	const fs::path scene_path = temp_directory / "vegetation_component_v7.scene.json";
	std::error_code filesystem_error{};
	fs::create_directories(temp_directory, filesystem_error);
	REQUIRE_FALSE(filesystem_error);

	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Roundtrip");
	const AshEngine::EntityId high_surface_id = (1ull << 60u) + 29u;
	AshEngine::Entity surface = scene.create_entity_with_id(high_surface_id, "Surface");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation");
	AshEngine::VegetationComponent persisted = MakeVegetation(surface.get_id());
	persisted.enabled = false;
	REQUIRE(vegetation.add_vegetation_component(persisted));

	std::string error_message{};
	REQUIRE(scene.save_to_file(scene_path, &error_message));
	CHECK(error_message.empty());
	json saved = ReadJson(scene_path);
	CHECK(saved.at("version") == 7u);
	const json& saved_component = saved.at("entities")[1].at("vegetation");
	CHECK(saved_component.at("layer_asset_path") == "vegetation/meadow.AshVegetationLayer");
	CHECK(saved_component.at("surface_entity_id") == high_surface_id);
	CHECK(saved_component.at("enabled") == false);

	json forward_reference = saved;
	std::swap(forward_reference["entities"][0], forward_reference["entities"][1]);
	WriteJson(scene_path, forward_reference);
	AshEngine::Scene loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
	REQUIRE_MESSAGE(loaded.is_valid(), error_message);
	const AshEngine::Entity loaded_vegetation = loaded.find_entity(vegetation.get_id());
	REQUIRE(loaded_vegetation.has_vegetation_component());
	CHECK(loaded_vegetation.get_vegetation_component().surface_entity_id == high_surface_id);
	CHECK_FALSE(loaded_vegetation.get_vegetation_component().enabled);

	for (uint32_t version = 3u; version <= 6u; ++version)
	{
		json legacy = saved;
		legacy["version"] = version;
		WriteJson(scene_path, legacy);
		AshEngine::Scene legacy_loaded = AshEngine::Scene::load_from_file(scene_path, &error_message);
		REQUIRE_MESSAGE(legacy_loaded.is_valid(), error_message);
		CHECK(legacy_loaded.extract_vegetation_entities().empty());
	}

	json invalid = saved;
	invalid["version"] = 7u;
	invalid["entities"][1]["vegetation"]["surface_entity_id"] = 0xfedcba9876543210ull;
	WriteJson(scene_path, invalid);
	CHECK_FALSE(AshEngine::Scene::load_from_file(scene_path, &error_message).is_valid());
	CHECK_FALSE(error_message.empty());

	json negative_reference = saved;
	negative_reference["entities"][0]["id"] = std::numeric_limits<uint64_t>::max();
	negative_reference["entities"][1]["vegetation"]["surface_entity_id"] = -1;
	WriteJson(scene_path, negative_reference);
	CHECK_FALSE(AshEngine::Scene::load_from_file(scene_path, &error_message).is_valid());
	CHECK_FALSE(error_message.empty());

	for (const json& invalid_surface_value : std::vector<json>{
		json(1.5),
		json("1"),
		json::array({ high_surface_id }),
	})
	{
		json invalid_type = saved;
		invalid_type["entities"][1]["vegetation"]["surface_entity_id"] = invalid_surface_value;
		WriteJson(scene_path, invalid_type);
		CHECK_FALSE(AshEngine::Scene::load_from_file(scene_path, &error_message).is_valid());
		CHECK_FALSE(error_message.empty());
	}

	fs::remove(scene_path, filesystem_error);
}

TEST_CASE("VegetationComponent snapshot restore creates the full subtree before applying references")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Snapshot Restore");
	AshEngine::Entity root = scene.create_entity("Root");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation", root);
	AshEngine::Entity surface = scene.create_entity("Surface", root);
	REQUIRE(root.is_valid());
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));

	const AshEngine::EntityId root_id = root.get_id();
	const AshEngine::EntityId vegetation_id = vegetation.get_id();
	const AshEngine::EntityId surface_id = surface.get_id();
	const std::optional<AshEditor::SceneEntitySnapshot> snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(scene, root_id);
	REQUIRE(snapshot.has_value());
	REQUIRE(scene.destroy_entity(root_id));

	AshEngine::Entity restored =
		AshEditor::SceneSnapshotUtils::RestoreEntitySnapshot(scene, *snapshot);
	REQUIRE(restored.is_valid());
	CHECK(restored.get_id() == root_id);
	REQUIRE(scene.find_entity(surface_id).is_valid());
	const AshEngine::Entity restored_vegetation = scene.find_entity(vegetation_id);
	REQUIRE(restored_vegetation.has_vegetation_component());
	CHECK(restored_vegetation.get_vegetation_component().surface_entity_id == surface_id);
}

TEST_CASE("VegetationComponent snapshot copy remaps internal references and preserves external references")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Snapshot Copy");
	AshEngine::Entity external_surface = scene.create_entity("External Surface");
	AshEngine::Entity root = scene.create_entity("Root");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation", root);
	AshEngine::Entity surface = scene.create_entity("Surface", root);
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));

	const std::optional<AshEditor::SceneEntitySnapshot> snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(scene, root.get_id());
	REQUIRE(snapshot.has_value());
	std::vector<AshEngine::EntityId> created_ids{};
	AshEngine::Entity copied = AshEditor::SceneSnapshotUtils::RestoreEntitySnapshotAsCopy(
		scene,
		*snapshot,
		0,
		AshEngine::k_scene_append_sibling_index,
		&created_ids,
		" Copy");
	REQUIRE(copied.is_valid());
	REQUIRE(created_ids.size() == 3u);
	const AshEngine::Entity copied_vegetation = scene.find_entity(created_ids[1]);
	const AshEngine::Entity copied_surface = scene.find_entity(created_ids[2]);
	REQUIRE(copied_vegetation.has_vegetation_component());
	CHECK(copied_vegetation.get_vegetation_component().surface_entity_id == copied_surface.get_id());
	CHECK(copied_surface.get_id() != surface.get_id());

	AshEngine::Entity external_root = scene.create_entity("External Root");
	REQUIRE(external_root.add_vegetation_component(MakeVegetation(external_surface.get_id())));
	const std::optional<AshEditor::SceneEntitySnapshot> external_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(scene, external_root.get_id());
	REQUIRE(external_snapshot.has_value());
	AshEngine::Entity external_copy = AshEditor::SceneSnapshotUtils::RestoreEntitySnapshotAsCopy(
		scene,
		*external_snapshot,
		0,
		AshEngine::k_scene_append_sibling_index);
	REQUIRE(external_copy.has_vegetation_component());
	CHECK(external_copy.get_vegetation_component().surface_entity_id == external_surface.get_id());
}

TEST_CASE("VegetationComponent snapshot copy rolls back a late invalid reference without leaking output ids")
{
	AshEngine::Scene source = AshEngine::Scene::create("Vegetation Snapshot Source");
	AshEngine::Entity root = source.create_entity("Root");
	AshEngine::Entity surface = source.create_entity("Surface", root);
	AshEngine::Entity vegetation = source.create_entity("Vegetation", root);
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));

	std::optional<AshEditor::SceneEntitySnapshot> snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(source, root.get_id());
	REQUIRE(snapshot.has_value());
	AshEditor::SceneEntitySnapshot* vegetation_snapshot =
		FindSnapshotEntity(*snapshot, vegetation.get_id());
	REQUIRE(vegetation_snapshot != nullptr);
	AshEditor::SceneComponentSnapshot* component_snapshot =
		FindSnapshotComponent(*vegetation_snapshot, AshEngine::SceneComponentType::Vegetation);
	REQUIRE(component_snapshot != nullptr);
	component_snapshot->strSerializedValue =
		R"({"layer_asset_path":"vegetation/meadow.AshVegetationLayer","surface_entity_id":18446744073709551614,"enabled":true})";

	AshEngine::Scene target = AshEngine::Scene::create("Vegetation Snapshot Target");
	const uint32_t before_count = target.get_entity_count();
	std::vector<AshEngine::EntityId> created_ids{ 77u };
	CHECK_FALSE(AshEditor::SceneSnapshotUtils::RestoreEntitySnapshotAsCopy(
		target,
		*snapshot,
		0,
		AshEngine::k_scene_append_sibling_index,
		&created_ids).is_valid());
	CHECK(target.get_entity_count() == before_count);
	CHECK(created_ids == std::vector<AshEngine::EntityId>{ 77u });
}

TEST_CASE("VegetationComponent CloneScene resolves a surface in a later root")
{
	AshEngine::Scene source = AshEngine::Scene::create("Vegetation Clone Source");
	AshEngine::Entity vegetation = source.create_entity("Vegetation Root");
	AshEngine::Entity surface = source.create_entity("Surface Root");
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));

	AshEngine::Scene clone = AshEditor::SceneSnapshotUtils::CloneScene(source);
	REQUIRE(clone.is_valid());
	const AshEngine::Entity cloned_vegetation = clone.find_entity(vegetation.get_id());
	REQUIRE(cloned_vegetation.has_vegetation_component());
	CHECK(cloned_vegetation.get_vegetation_component().surface_entity_id == surface.get_id());
}

TEST_CASE("SceneSnapshotUtils CloneScene preserves an empty valid scene")
{
	AshEngine::Scene source = AshEngine::Scene::create("Empty Clone");
	REQUIRE(source.is_valid());
	source.mark_clean();

	AshEngine::Scene cloned = AshEditor::SceneSnapshotUtils::CloneScene(source);
	REQUIRE(cloned.is_valid());
	CHECK(cloned.get_name() == "Empty Clone");
	CHECK(cloned.get_entity_count() == 0u);
	CHECK_FALSE(cloned.is_dirty());
}

TEST_CASE("Scene creation transactions reject reload and replace while preserving both scenes")
{
	const fs::path temp_directory = fs::path("Intermediate") / "test-temp" / "scene";
	const fs::path scene_path = temp_directory / "vegetation_transaction_reload.scene.json";
	std::error_code filesystem_error{};
	fs::create_directories(temp_directory, filesystem_error);
	REQUIRE_FALSE(filesystem_error);

	AshEngine::Scene replacement = AshEngine::Scene::create("Replacement");
	const AshEngine::Entity replacement_entity = replacement.create_entity("Replacement Entity");
	REQUIRE(replacement_entity.is_valid());
	std::string error_message{};
	REQUIRE(replacement.save_to_file(scene_path, &error_message));

	AshEngine::Scene target = AshEngine::Scene::create("Target");
	const AshEngine::Entity original = target.create_entity("Original");
	REQUIRE(original.is_valid());
	{
		AshEngine::Scene::CreationTransaction transaction = target.begin_creation_transaction();
		REQUIRE(transaction.is_active());
		const AshEngine::Entity transient = target.create_entity("Transient");
		REQUIRE(transient.is_valid());
		CHECK_FALSE(target.reload_from_file(scene_path, &error_message));
		CHECK_FALSE(error_message.empty());
		target.replace_contents(std::move(replacement));
		CHECK(target.get_name() == "Target");
		CHECK(target.find_entity(original.get_id()).is_valid());
		CHECK(target.find_entity(transient.get_id()).is_valid());
		CHECK(replacement.is_valid());
		CHECK(replacement.find_entity(replacement_entity.get_id()).is_valid());
	}
	CHECK(target.find_entity(original.get_id()).is_valid());
	CHECK(target.get_entity_count() == 1u);

	AshEngine::Scene active_source = AshEngine::Scene::create("Active Source");
	const AshEngine::Entity source_original = active_source.create_entity("Source Original");
	REQUIRE(source_original.is_valid());
	{
		AshEngine::Scene::CreationTransaction transaction = active_source.begin_creation_transaction();
		REQUIRE(transaction.is_active());
		const AshEngine::Entity source_transient = active_source.create_entity("Source Transient");
		REQUIRE(source_transient.is_valid());
		target.replace_contents(std::move(active_source));
		CHECK(target.get_name() == "Target");
		CHECK(target.find_entity(original.get_id()).is_valid());
		CHECK(active_source.get_name() == "Active Source");
		CHECK(active_source.is_valid());
		CHECK(active_source.find_entity(source_transient.get_id()).is_valid());
	}
	CHECK(active_source.find_entity(source_original.get_id()).is_valid());
	CHECK(active_source.get_entity_count() == 1u);

	fs::remove(scene_path, filesystem_error);
}

TEST_CASE("Scene creation transactions buffer public change notifications until commit")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Transaction Events");
	uint32_t event_count = 0;
	const uint32_t subscription = scene.subscribe_change_events(
		[&event_count](const AshEngine::SceneChangeEvent&)
		{
			++event_count;
		});
	REQUIRE(subscription != 0u);

	AshEngine::SceneChangeEvent event{};
	event.kind = AshEngine::SceneChangeKind::ComponentChanged;
	{
		AshEngine::Scene::CreationTransaction transaction = scene.begin_creation_transaction();
		REQUIRE(transaction.is_active());
		scene.notify_change_event(event);
		CHECK(event_count == 0u);
	}
	CHECK(event_count == 0u);
	{
		AshEngine::Scene::CreationTransaction transaction = scene.begin_creation_transaction();
		REQUIRE(transaction.is_active());
		scene.notify_change_event(event);
		CHECK(event_count == 0u);
		REQUIRE(transaction.commit());
	}
	CHECK(event_count == 1u);
	CHECK(scene.unsubscribe_change_events(subscription));
}

TEST_CASE("VegetationComponent multi root snapshot copy shares one reference remap")
{
	AshEngine::Scene source = AshEngine::Scene::create("Vegetation Multi Root Source");
	AshEngine::Entity vegetation = source.create_entity("Vegetation Root");
	AshEngine::Entity surface = source.create_entity("Surface Root");
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));
	const std::optional<AshEditor::SceneEntitySnapshot> vegetation_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(source, vegetation.get_id());
	const std::optional<AshEditor::SceneEntitySnapshot> surface_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(source, surface.get_id());
	REQUIRE(vegetation_snapshot.has_value());
	REQUIRE(surface_snapshot.has_value());

	AshEngine::Scene target = AshEngine::Scene::create("Vegetation Multi Root Target");
	const std::vector<AshEditor::SceneSnapshotUtils::SceneSnapshotRestoreRequest> requests = {
		{ &*vegetation_snapshot, 0, AshEngine::k_scene_append_sibling_index, " Copy" },
		{ &*surface_snapshot, 0, AshEngine::k_scene_append_sibling_index, " Copy" },
	};
	std::vector<AshEngine::EntityId> copied_roots{};
	REQUIRE(AshEditor::SceneSnapshotUtils::RestoreEntitySnapshotsAsCopies(
		target,
		requests,
		copied_roots));
	REQUIRE(copied_roots.size() == 2u);
	const AshEngine::Entity copied_vegetation = target.find_entity(copied_roots[0]);
	REQUIRE(copied_vegetation.has_vegetation_component());
	CHECK(copied_vegetation.get_vegetation_component().surface_entity_id == copied_roots[1]);
}

TEST_CASE("VegetationComponent multi root restore is atomic and preserves caller outputs on failure")
{
	AshEngine::Scene source = AshEngine::Scene::create("Vegetation Atomic Forest Source");
	AshEngine::Entity first_root = source.create_entity("First Root");
	AshEngine::Entity surface = source.create_entity("Surface Root");
	AshEngine::Entity vegetation = source.create_entity("Vegetation Root");
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));

	std::optional<AshEditor::SceneEntitySnapshot> first_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(source, first_root.get_id());
	std::optional<AshEditor::SceneEntitySnapshot> vegetation_snapshot =
		AshEditor::SceneSnapshotUtils::CaptureEntitySnapshot(source, vegetation.get_id());
	REQUIRE(first_snapshot.has_value());
	REQUIRE(vegetation_snapshot.has_value());
	AshEditor::SceneComponentSnapshot* vegetation_component =
		FindSnapshotComponent(*vegetation_snapshot, AshEngine::SceneComponentType::Vegetation);
	REQUIRE(vegetation_component != nullptr);
	vegetation_component->strSerializedValue =
		R"({"layer_asset_path":"vegetation/meadow.AshVegetationLayer","surface_entity_id":18446744073709551614,"enabled":true})";

	AshEngine::Scene target = AshEngine::Scene::create("Vegetation Atomic Forest Target");
	target.mark_clean();
	const uint32_t before_count = target.get_entity_count();
	const uint64_t before_change_version = target.get_change_version();
	const uint64_t before_content_epoch = target.get_content_epoch();
	const uint64_t before_primitive_version = target.get_render_primitive_version();
	const uint64_t before_transform_version = target.get_render_transform_version();
	const uint64_t before_light_version = target.get_render_light_version();
	const uint64_t before_environment_version = target.get_render_environment_version();
	const uint64_t before_particle_version = target.get_render_particle_version();
	const uint64_t before_vegetation_version = target.get_vegetation_version();
	const uint64_t before_config_version = target.get_render_config_version();
	uint32_t event_count = 0;
	const uint32_t subscription = target.subscribe_change_events(
		[&event_count](const AshEngine::SceneChangeEvent&)
		{
			++event_count;
		});
	REQUIRE(subscription != 0u);
	const std::vector<AshEditor::SceneSnapshotUtils::SceneSnapshotRestoreRequest> requests = {
		{ &*first_snapshot, 0, AshEngine::k_scene_append_sibling_index, " Copy" },
		{ &*vegetation_snapshot, 0, AshEngine::k_scene_append_sibling_index, " Copy" },
	};
	std::vector<AshEngine::EntityId> root_ids{ 77u };
	std::vector<AshEngine::EntityId> created_ids{ 88u };
	CHECK_FALSE(AshEditor::SceneSnapshotUtils::RestoreEntitySnapshotsAsCopies(
		target,
		requests,
		root_ids,
		&created_ids));
	CHECK(target.get_entity_count() == before_count);
	CHECK(root_ids == std::vector<AshEngine::EntityId>{ 77u });
	CHECK(created_ids == std::vector<AshEngine::EntityId>{ 88u });
	CHECK_FALSE(target.is_dirty());
	CHECK(target.get_change_version() == before_change_version);
	CHECK(target.get_content_epoch() == before_content_epoch);
	CHECK(target.get_render_primitive_version() == before_primitive_version);
	CHECK(target.get_render_transform_version() == before_transform_version);
	CHECK(target.get_render_light_version() == before_light_version);
	CHECK(target.get_render_environment_version() == before_environment_version);
	CHECK(target.get_render_particle_version() == before_particle_version);
	CHECK(target.get_vegetation_version() == before_vegetation_version);
	CHECK(target.get_render_config_version() == before_config_version);
	CHECK(event_count == 0u);

	const std::vector<AshEditor::SceneSnapshotUtils::SceneSnapshotRestoreRequest> invalid_parent = {
		{ &*first_snapshot, 0xfedcba9876543210ull, AshEngine::k_scene_append_sibling_index, nullptr },
	};
	CHECK_FALSE(AshEditor::SceneSnapshotUtils::RestoreEntitySnapshots(
		target,
		invalid_parent,
		root_ids));
	CHECK(target.get_entity_count() == before_count);
	CHECK(root_ids == std::vector<AshEngine::EntityId>{ 77u });
	CHECK(event_count == 0u);
	CHECK(target.unsubscribe_change_events(subscription));
	CHECK(target.create_entity("Next Entity").get_id() == 1u);
}

TEST_CASE("VegetationComponent surface deletion is transactional and subtree deletion bumps once")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Delete References");
	AshEngine::Entity surface = scene.create_entity("Surface");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation");
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));
	const uint32_t before_count = scene.get_entity_count();
	const uint64_t before_version = scene.get_vegetation_version();
	CHECK_FALSE(scene.destroy_entity(surface.get_id()));
	CHECK(scene.get_entity_count() == before_count);
	CHECK(scene.find_entity(surface.get_id()).is_valid());
	CHECK(scene.find_entity(vegetation.get_id()).is_valid());
	CHECK(scene.get_vegetation_version() == before_version);

	AshEngine::Entity root_surface = scene.create_entity("Root Surface");
	AshEngine::Entity first = scene.create_entity("First Vegetation", root_surface);
	AshEngine::Entity second = scene.create_entity("Second Vegetation", root_surface);
	REQUIRE(first.add_vegetation_component(MakeVegetation(root_surface.get_id())));
	REQUIRE(second.add_vegetation_component(MakeVegetation(root_surface.get_id())));
	const AshEngine::EntityId root_surface_id = root_surface.get_id();
	const AshEngine::EntityId first_id = first.get_id();
	const AshEngine::EntityId second_id = second.get_id();
	const uint64_t before_subtree_delete = scene.get_vegetation_version();
	REQUIRE(scene.destroy_entity(root_surface_id));
	CHECK_FALSE(scene.find_entity(root_surface_id).is_valid());
	CHECK_FALSE(scene.find_entity(first_id).is_valid());
	CHECK_FALSE(scene.find_entity(second_id).is_valid());
	CHECK(scene.get_vegetation_version() == before_subtree_delete + 1u);

	AshEngine::Scene blocked_scene = AshEngine::Scene::create("Blocked Batch Delete");
	AshEngine::Entity blocked_surface = blocked_scene.create_entity("Blocked Surface");
	AshEngine::Entity blocked_vegetation = blocked_scene.create_entity("Blocked Vegetation");
	AshEngine::Entity unrelated = blocked_scene.create_entity("Unrelated");
	REQUIRE(blocked_vegetation.add_vegetation_component(MakeVegetation(blocked_surface.get_id())));
	blocked_scene.mark_clean();
	const uint32_t blocked_count = blocked_scene.get_entity_count();
	const uint64_t blocked_version = blocked_scene.get_vegetation_version();
	const uint64_t blocked_change_version = blocked_scene.get_change_version();
	const uint64_t blocked_primitive_version = blocked_scene.get_render_primitive_version();
	uint32_t blocked_event_count = 0;
	const uint32_t subscription = blocked_scene.subscribe_change_events(
		[&blocked_event_count](const AshEngine::SceneChangeEvent&)
		{
			++blocked_event_count;
		});
	REQUIRE(subscription != 0u);
	CHECK_FALSE(blocked_scene.destroy_entities({ unrelated.get_id(), blocked_surface.get_id() }));
	CHECK(blocked_scene.get_entity_count() == blocked_count);
	CHECK(blocked_scene.find_entity(unrelated.get_id()).is_valid());
	CHECK(blocked_scene.find_entity(blocked_surface.get_id()).is_valid());
	CHECK(blocked_scene.find_entity(blocked_vegetation.get_id()).is_valid());
	CHECK(blocked_scene.get_vegetation_version() == blocked_version);
	CHECK(blocked_scene.get_change_version() == blocked_change_version);
	CHECK(blocked_scene.get_render_primitive_version() == blocked_primitive_version);
	CHECK_FALSE(blocked_scene.is_dirty());
	CHECK(blocked_event_count == 0u);
	CHECK(blocked_scene.unsubscribe_change_events(subscription));
}

TEST_CASE("VegetationComponent batch deletion treats cross root references as one transaction")
{
	AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Batch Delete");
	AshEngine::Entity vegetation = scene.create_entity("Vegetation Root");
	AshEngine::Entity surface = scene.create_entity("Surface Root");
	REQUIRE(vegetation.add_vegetation_component(MakeVegetation(surface.get_id())));
	const AshEngine::EntityId vegetation_id = vegetation.get_id();
	const AshEngine::EntityId surface_id = surface.get_id();
	const uint64_t before = scene.get_vegetation_version();
	std::vector<std::tuple<AshEngine::EntityId, uint64_t, uint64_t, uint64_t>> removed_events{};
	const uint32_t subscription = scene.subscribe_change_events(
		[&scene, &removed_events](const AshEngine::SceneChangeEvent& event)
		{
			if (event.kind == AshEngine::SceneChangeKind::EntityRemoved)
			{
				removed_events.emplace_back(
					event.entity_id,
					event.change_version,
					scene.get_render_primitive_version(),
					scene.get_vegetation_version());
			}
		});
	REQUIRE(subscription != 0u);

	REQUIRE(scene.destroy_entities({ vegetation_id, surface_id }));
	CHECK(scene.get_entity_count() == 0u);
	CHECK(scene.get_vegetation_version() == before + 1u);
	REQUIRE(removed_events.size() == 2u);
	CHECK(std::get<0>(removed_events[0]) == vegetation_id);
	CHECK(std::get<0>(removed_events[1]) == surface_id);
	CHECK(std::get<1>(removed_events[0]) == std::get<1>(removed_events[1]));
	CHECK(std::get<2>(removed_events[0]) == std::get<2>(removed_events[1]));
	CHECK(std::get<3>(removed_events[0]) == std::get<3>(removed_events[1]));
	CHECK(std::get<3>(removed_events[0]) == scene.get_vegetation_version());
	CHECK(scene.unsubscribe_change_events(subscription));
}
