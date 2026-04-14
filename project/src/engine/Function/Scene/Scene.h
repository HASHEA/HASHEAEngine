#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
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

	enum class SceneComponentType : uint8_t
	{
		Name = 0,
		Transform,
		Camera,
		Light,
		Mesh
	};

	enum class ScenePropertyType : uint8_t
	{
		Bool = 0,
		Int32,
		UInt32,
		Float,
		Vec2,
		Vec3,
		Vec4,
		String,
		Enum
	};

	struct NameComponent
	{
		std::string value = "Entity";
	};

	struct TransformComponent
	{
		glm::vec3 position{ 0.0f, 0.0f, 0.0f };
		glm::vec3 rotation_euler_degrees{ 0.0f, 0.0f, 0.0f };
		glm::vec3 scale{ 1.0f, 1.0f, 1.0f };
	};

	enum class CameraProjectionType : uint8_t
	{
		Perspective = 0,
		Orthographic
	};

	struct CameraComponent
	{
		bool primary = true;
		CameraProjectionType projection = CameraProjectionType::Perspective;
		float fov_y_degrees = 60.0f;
		float near_plane = 0.1f;
		float far_plane = 1000.0f;
		float orthographic_height = 10.0f;
	};

	enum class LightType : uint8_t
	{
		Directional = 0,
		Point,
		Spot
	};

	struct LightComponent
	{
		LightType type = LightType::Directional;
		glm::vec3 color{ 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float range = 10.0f;
		float inner_cone_angle_degrees = 30.0f;
		float outer_cone_angle_degrees = 45.0f;
	};

	struct MeshComponent
	{
		std::string asset_path{};
		bool visible = true;
	};

	struct SceneEnumValueDesc
	{
		int32_t value = 0;
		const char* name = nullptr;
	};

	struct SceneEnumDesc
	{
		const char* name = nullptr;
		const SceneEnumValueDesc* values = nullptr;
		uint32_t value_count = 0;
	};

	struct ScenePropertyDesc
	{
		const char* name = nullptr;
		ScenePropertyType type = ScenePropertyType::Float;
		uint32_t offset = 0;
		uint32_t size = 0;
		const char* enum_name = nullptr;
	};

	struct SceneComponentDesc
	{
		SceneComponentType type = SceneComponentType::Name;
		const char* name = nullptr;
		const ScenePropertyDesc* properties = nullptr;
		uint32_t property_count = 0;
		uint32_t byte_size = 0;
	};

	class Scene;

	class ASH_API Entity
	{
	public:
		Entity() = default;

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
		Entity create_child(std::string_view name = "Entity");
		bool destroy();

	private:
		class Impl;

	private:
		explicit Entity(std::shared_ptr<Impl> impl, EntityId id);
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
		Entity find_entity(EntityId id) const;
		std::vector<Entity> get_entities() const;
		std::vector<Entity> get_root_entities() const;
		uint32_t get_entity_count() const;

		bool save_to_file(const std::filesystem::path& path, std::string* out_error = nullptr);
		const std::filesystem::path& get_source_path() const;

		bool is_dirty() const;
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
