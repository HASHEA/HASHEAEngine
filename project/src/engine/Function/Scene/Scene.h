#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Scene/SceneConfig.h"
#include "Function/Scene/SceneComponents.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	using EntityId = uint64_t;
	static constexpr uint32_t k_scene_append_sibling_index = std::numeric_limits<uint32_t>::max();

	struct Model;
	struct AshAsset;

	struct Mesh;

	// editor begin 修改原因：Scene Change Event 语义，支持 Editor 根据明确事件刷新
	enum class SceneChangeKind : uint8_t
	{
		EntityAdded = 0,
		EntityRemoved,
		HierarchyChanged,
		ComponentChanged,
		SceneReplaced,
		SceneReloaded,
		DirtyStateChanged
	};

	struct ASH_API SceneChangeEvent
	{
		SceneChangeKind kind = SceneChangeKind::ComponentChanged;
		EntityId entity_id = 0;
		SceneComponentType component_type = SceneComponentType::Name;
		uint64_t change_version = 0;
		bool dirty = false;
	};

	using SceneChangeCallback = std::function<void(const SceneChangeEvent&)>;
	// editor end

	struct ASH_API SceneMeshExtractionDesc
	{
		EntityId entity_id = 0;
		std::string asset_path{};
		uint32_t mesh_index = 0;
		std::vector<MeshMaterialOverride> material_overrides{};
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

	struct ASH_API SceneLightExtractionDesc
	{
		EntityId entity_id = 0;
		LightComponent light{};
		glm::mat4 world_transform{ 1.0f };
	};

	struct ASH_API SceneEnvironmentExtractionDesc
	{
		EntityId entity_id = 0;
		std::string ibl_asset_path{};
		std::string source_texture_path{};
		float intensity = 1.0f;
		float lighting_intensity = 1.0f;
		float background_intensity = 1.0f;
		float rotation_degrees = 0.0f;
		bool visible_background = true;
		bool affect_lighting = true;
	};

	struct ASH_API SceneParticleExtractionDesc
	{
		EntityId entity_id = 0;
		ParticleComponent particle{};
		glm::mat4 world_transform{ 1.0f };
	};

	struct ASH_API SceneTerrainExtractionDesc
	{
		EntityId entity_id = 0;
		TerrainComponent terrain{};
		glm::mat4 world_transform{ 1.0f };
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
		// editor begin 修改原因：供编辑器预览相机等临时实体静默更新 Transform，避免走正式编辑变更路径。
		bool set_transform_component_silent(const TransformComponent& component);
		// editor end

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

		bool has_environment_component() const;
		EnvironmentComponent get_environment_component() const;
		bool add_environment_component(const EnvironmentComponent& component = {});
		bool set_environment_component(const EnvironmentComponent& component);
		bool remove_environment_component();

		bool has_particle_component() const;
		ParticleComponent get_particle_component() const;
		bool add_particle_component(const ParticleComponent& component = {});
		bool set_particle_component(const ParticleComponent& component);
		bool remove_particle_component();

		bool has_terrain_component() const;
		TerrainComponent get_terrain_component() const;
		bool add_terrain_component(const TerrainComponent& component);
		bool set_terrain_component(const TerrainComponent& component);
		bool remove_terrain_component();

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

	struct ASH_API SceneInstantiationDesc
	{
		Entity parent{};
		glm::vec3 world_position{ 0.0f };
		glm::vec3 world_rotation_euler_degrees{ 0.0f };
		glm::vec3 world_scale{ 1.0f };
		bool use_world_transform = false;
		std::string root_name_override{};
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
		// editor begin 修改原因：Scene Reload / Replace 语义，保留 change event 订阅者
		bool reload_from_file(const std::filesystem::path& path, std::string* out_error = nullptr);
		void replace_contents(Scene&& other);
		// editor end

		bool is_valid() const;
		const std::string& get_name() const;
		void set_name(std::string_view name);

		Entity create_entity(std::string_view name = "Entity");
		Entity create_entity(std::string_view name, const Entity& parent);
		Entity create_entity(std::string_view name, const Entity& parent, uint32_t sibling_index);
		Entity create_entity_with_id(EntityId explicit_id, std::string_view name = "Entity");
		Entity create_entity_with_id(EntityId explicit_id, std::string_view name, const Entity& parent);
		Entity create_entity_with_id(EntityId explicit_id, std::string_view name, const Entity& parent, uint32_t sibling_index);
		bool destroy_entity(EntityId id);
		bool reparent_entity(EntityId id, EntityId new_parent_id);
		bool reparent_entity(EntityId id, EntityId new_parent_id, uint32_t sibling_index);
		uint32_t get_entity_sibling_index(EntityId id) const;
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
		std::vector<SceneLightExtractionDesc> extract_light_entities() const;
		std::vector<SceneParticleExtractionDesc> extract_particle_entities() const;
		std::vector<SceneTerrainExtractionDesc> extract_terrain_entities() const;
		bool extract_active_environment(SceneEnvironmentExtractionDesc& out_environment) const;
		bool try_get_mesh_local_bounds(AssetDatabase& database, const MeshComponent& mesh_component, SceneMeshBounds& out_bounds) const;

		Entity instantiate_model(const Model& model, const Entity& parent = {}, std::string_view root_name_override = {});
		Entity instantiate_ashasset(const AshAsset& asset, const Entity& parent = {}, std::string_view root_name_override = {});
		Entity instantiate_asset(AssetDatabase& database, const std::filesystem::path& path, const Entity& parent = {});
		// editor begin 修改原因：支持 Mesh 资源通过 AssetId 实例化到场景
		Entity instantiate_mesh(const std::shared_ptr<const Mesh>& mesh, const std::filesystem::path& asset_path, const Entity& parent = {}, std::string_view root_name_override = {});
		// editor end

		bool save_to_file(const std::filesystem::path& path, std::string* out_error = nullptr);
		const std::filesystem::path& get_source_path() const;

		bool is_dirty() const;
		uint64_t get_change_version() const;
		// Process-local content identity. Component edits keep the epoch; full reload
		// or replace advances it so stateful render systems can discard old history.
		uint64_t get_content_epoch() const;
		const SceneRenderConfig& get_render_config() const;
		bool set_render_config(const SceneRenderConfig& config);
		uint64_t get_render_primitive_version() const;
		uint64_t get_render_transform_version() const;
		uint64_t get_render_light_version() const;
		uint64_t get_render_environment_version() const;
		uint64_t get_render_particle_version() const;
		uint64_t get_render_terrain_version() const;
		uint64_t get_render_config_version() const;
		void mark_clean();

		// editor begin 修改原因：Scene Change Event 订阅接口
		uint32_t subscribe_change_events(SceneChangeCallback callback);
		bool unsubscribe_change_events(uint32_t subscription_id);
		void notify_change_event(const SceneChangeEvent& event);
		// editor end

	private:
		std::shared_ptr<Impl> m_impl{};

	private:
		explicit Scene(std::shared_ptr<Impl> impl);
		friend class Entity;
	};

	ASH_API Entity instantiate_asset(
		Scene& scene,
		AssetDatabase& database,
		AssetId asset_id,
		const SceneInstantiationDesc& desc = {});

	ASH_API const SceneComponentDesc* get_scene_component_descriptor(SceneComponentType type);
	ASH_API const SceneComponentDesc* get_scene_component_descriptors(uint32_t* out_count);
	ASH_API const SceneEnumDesc* get_scene_enum_descriptor(const char* name);

	// editor begin 修改原因：通用 Add/Remove Component facade
	ASH_API bool can_add_scene_component(const Entity& entity, SceneComponentType type);
	ASH_API bool can_remove_scene_component(const Entity& entity, SceneComponentType type);
	ASH_API bool add_scene_component(Entity& entity, SceneComponentType type);
	ASH_API bool remove_scene_component(Entity& entity, SceneComponentType type);
	// editor end
}
