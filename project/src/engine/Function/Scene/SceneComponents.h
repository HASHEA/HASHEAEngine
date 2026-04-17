#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace AshEngine
{
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

	enum class SceneMobility : uint8_t
	{
		Static = 0,
		Stationary,
		Movable
	};

	static constexpr uint32_t k_default_scene_layer_mask = 0x1u;

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
		uint32_t mesh_index = 0;
		bool visible = true;
		SceneMobility mobility = SceneMobility::Static;
		uint32_t layer_mask = k_default_scene_layer_mask;
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
}
