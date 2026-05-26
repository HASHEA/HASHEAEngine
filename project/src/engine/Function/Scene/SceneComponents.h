#pragma once

#include "Base/hcore.h"
#include "Base/hplatform.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	enum class SceneComponentType : uint8_t
	{
		Name = 0,
		Transform,
		Camera,
		Light,
		Mesh,
		Environment
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

	// editor begin 修改原因：组件元数据增强，供 Editor Inspector 读取 hint / range / asset ref
	enum class ScenePropertyEditorHint : uint8_t
	{
		Default = 0,
		Slider,
		Color,
		AssetPath,
		Hidden
	};

	enum class ScenePropertyAssetRefKind : uint8_t
	{
		None = 0,
		Mesh,
		Material,
		Texture,
		IBL
	};
	// editor end

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
		bool reverse_z = true;
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
	static constexpr uint32_t k_invalid_material_slot = 0xffffffffu;

	struct LightComponent
	{
		LightType type = LightType::Directional;
		glm::vec3 color{ 1.0f, 1.0f, 1.0f };
		float intensity = 1.0f;
		float range = 10.0f;
		float inner_cone_angle_degrees = 30.0f;
		float outer_cone_angle_degrees = 45.0f;
		bool casts_shadow = true;
		bool sunlight = false;
		uint32_t shadow_priority = 128;
		float shadow_distance = 0.0f;
		uint32_t shadow_cascade_count = 0;
		float near_shadow_distance = 0.0f;
	};

	struct MeshMaterialOverride
	{
		uint32_t material_slot = k_invalid_material_slot;
		std::string material_path{};
	};

	struct MeshComponent
	{
		std::string asset_path{};
		uint32_t mesh_index = 0;
		std::vector<MeshMaterialOverride> material_overrides{};
		bool visible = true;
		SceneMobility mobility = SceneMobility::Static;
		uint32_t layer_mask = k_default_scene_layer_mask;
	};

	struct EnvironmentComponent
	{
		bool active = true;
		std::string ibl_asset_path{};
		std::string source_texture_path{};
		float intensity = 1.0f;
		float lighting_intensity = 1.0f;
		float background_intensity = 1.0f;
		float rotation_degrees = 0.0f;
		bool visible_background = true;
		bool affect_lighting = true;
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
		// editor begin 修改原因：组件元数据增强
		const char* display_name = nullptr;
		const char* tooltip = nullptr;
		ScenePropertyEditorHint editor_hint = ScenePropertyEditorHint::Default;
		ScenePropertyAssetRefKind asset_ref_kind = ScenePropertyAssetRefKind::None;
		float range_min = 0.0f;
		float range_max = 0.0f;
		bool use_range = false;
		bool read_only = false;
		// editor end
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
