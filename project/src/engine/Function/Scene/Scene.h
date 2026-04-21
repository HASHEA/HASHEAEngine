#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Function/Scene/SceneComponents.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	using EntityId = uint64_t;

	class AssetDatabase;
	struct Model;
	struct AshAsset;

	struct Mesh;

	struct ASH_API SceneMeshExtractionDesc
	{
		EntityId entity_id = 0;
		std::string asset_path{};
		uint32_t mesh_index = 0;
		bool visible = true;
		SceneMobility mobility = SceneMobility::Static;
		uint32_t layer_mask = k_default_scene_layer_mask;
		glm::mat4 world_transform{ 1.0f };
	};

	struct ASH_API SceneMeshBounds
	{
		bool is_valid = false;
		glm::vec3 local_min{ 0.0f, 0.0f, 0.0f };
		glm::vec3 local_max{ 0.0f, 0.0f, 0.0f };
	};

	class Scene;

	class ASH_API Entity
	{
	private:
		class Impl;

	public:
		Entity() = default;
		explicit Entity(std::shared_ptr<Impl> impl, EntityId id);

	public:
		bool is_valid() const;
		EntityId get_id() const;

		NameComponent get_name_component() const;
		bool set_name_component(const NameComponent& component);
		std::string get_name() const;
		bool set_name(std::string_view name);

		TransformComponent get_transform_component() const;
		bool set_transform_component(const TransformComponent& component);

		bool has_camera_component() const;
		CameraComponent get_camera_component() const;
		bool add_camera_component(const CameraComponent& component = {});
		bool set_camera_component(const CameraComponent& component);
		bool remove_camera_component();

		bool has_light_component() const;
		LightComponent get_light_component() const;
		bool add_light_component(const LightComponent& component = {});
		bool set_light_component(const LightComponent& component);
		bool remove_light_component();

		bool has_mesh_component() const;
		MeshComponent get_mesh_component() const;
		bool add_mesh_component(const MeshComponent& component = {});
		bool set_mesh_component(const MeshComponent& component);
		bool remove_mesh_component();

		bool has_component(SceneComponentType type) const;
		std::vector<SceneComponentType> get_component_types() const;
		bool read_component(SceneComponentType type, void* out_component, size_t component_size) const;
		bool write_component(SceneComponentType type, const void* component_data, size_t component_size);

		Entity get_parent() const;
		std::vector<Entity> get_children() const;
		bool set_parent(const Entity& parent);
		bool clear_parent();
		Entity create_child(std::string_view name = "Entity");
		bool destroy();

	private:
		std::shared_ptr<Impl> m_impl{};
		EntityId m_id = 0;
		friend class Scene;
	};

	class ASH_API Scene
	{
	public:
		class Impl;

	public:
		Scene() = default;

	public:
		static Scene create(std::string_view name = "Untitled Scene");
		static Scene load_from_file(const std::filesystem::path& path, std::string* out_error = nullptr);

		bool is_valid() const;
		const std::string& get_name() const;
		void set_name(std::string_view name);

		Entity create_entity(std::string_view name = "Entity");
		Entity create_entity(std::string_view name, const Entity& parent);
		bool destroy_entity(EntityId id);
		bool reparent_entity(EntityId id, EntityId new_parent_id);
		Entity find_entity(EntityId id) const;
		std::vector<Entity> get_entities() const;
		std::vector<Entity> get_root_entities() const;
		std::vector<Entity> get_entities_with_component(SceneComponentType type) const;
		std::vector<Entity> get_entities_with_components(const std::vector<SceneComponentType>& required_types) const;
		uint32_t get_entity_count() const;
		glm::mat4 get_entity_local_transform(EntityId id) const;
		glm::mat4 get_entity_world_transform(EntityId id) const;
		std::vector<SceneMeshExtractionDesc> extract_mesh_entities() const;
		std::vector<SceneMeshExtractionDesc> extract_visible_mesh_entities() const;
		bool try_get_mesh_local_bounds(AssetDatabase& database, const MeshComponent& mesh_component, SceneMeshBounds& out_bounds) const;

		Entity instantiate_model(const Model& model, const Entity& parent = {}, std::string_view root_name_override = {});
		Entity instantiate_ashasset(const AshAsset& asset, const Entity& parent = {}, std::string_view root_name_override = {});
		Entity instantiate_asset(AssetDatabase& database, const std::filesystem::path& path, const Entity& parent = {});

		bool save_to_file(const std::filesystem::path& path, std::string* out_error = nullptr);
		const std::filesystem::path& get_source_path() const;

		bool is_dirty() const;
		uint64_t get_change_version() const;
		void mark_clean();

	private:
		std::shared_ptr<Impl> m_impl{};

	private:
		explicit Scene(std::shared_ptr<Impl> impl);
		friend class Entity;
	};

	ASH_API const SceneComponentDesc* get_scene_component_descriptor(SceneComponentType type);
	ASH_API const SceneComponentDesc* get_scene_component_descriptors(uint32_t* out_count);
	ASH_API const SceneEnumDesc* get_scene_enum_descriptor(const char* name);
}
