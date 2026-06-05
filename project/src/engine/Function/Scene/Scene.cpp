#include "Scene.h"

#include "Base/hlog.h"
#include "Function/Asset/AssetData.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/EnvironmentMapAsset.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <json.hpp>
#include <type_traits>
#include <unordered_map>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <atomic>

namespace AshEngine
{
	namespace
	{
		using json = nlohmann::json;

		struct EntityIdComponent
		{
			EntityId id = 0;
		};

		struct HierarchyComponent
		{
			EntityId parent = 0;
			std::vector<EntityId> children{};
		};

		struct SceneStorage
		{
			std::string name = "Untitled Scene";
			std::filesystem::path source_path{};
			entt::registry registry{};
			std::unordered_map<EntityId, entt::entity> entities{};
			std::vector<EntityId> entity_order{};
			std::vector<EntityId> root_order{};
			EntityId next_entity_id = 1;
			bool dirty = false;
			uint64_t change_version = 0;
			SceneRenderConfig render_config = make_default_scene_render_config();
			uint64_t render_primitive_version = 0;
			uint64_t render_transform_version = 0;
			uint64_t render_light_version = 0;
			uint64_t render_environment_version = 0;
			uint64_t render_config_version = 0;
			// editor begin 修改原因：Scene Change Event 订阅管理
			std::unordered_map<uint32_t, SceneChangeCallback> change_callbacks{};
			uint32_t next_subscription_id = 1;
			// editor end
		};

		static constexpr uint32_t k_scene_file_version = 4;
		static constexpr const char* k_environment_sun_light_name = "EnvironmentSunLight";
		static constexpr float k_environment_sun_light_intensity = 2.5f;
		static constexpr uint32_t k_environment_sun_light_shadow_priority = 255u;
		static std::atomic<uint64_t> g_scene_change_version_seed{ 1 };

		static SceneEnumValueDesc k_camera_projection_values[] =
		{
			{ static_cast<int32_t>(CameraProjectionType::Perspective), "Perspective" },
			{ static_cast<int32_t>(CameraProjectionType::Orthographic), "Orthographic" },
		};

		static SceneEnumValueDesc k_light_type_values[] =
		{
			{ static_cast<int32_t>(LightType::Directional), "Directional" },
			{ static_cast<int32_t>(LightType::Point), "Point" },
			{ static_cast<int32_t>(LightType::Spot), "Spot" },
		};

		static SceneEnumDesc k_scene_enum_descs[] =
		{
			{ "CameraProjectionType", k_camera_projection_values, static_cast<uint32_t>(std::size(k_camera_projection_values)) },
			{ "LightType", k_light_type_values, static_cast<uint32_t>(std::size(k_light_type_values)) },
		};

		static ScenePropertyDesc k_name_properties[] =
		{
			{ "value", ScenePropertyType::String, static_cast<uint32_t>(offsetof(NameComponent, value)), static_cast<uint32_t>(sizeof(std::string)), nullptr },
		};

		static ScenePropertyDesc k_transform_properties[] =
		{
			{ "position", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, position)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
			{ "rotation_euler_degrees", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, rotation_euler_degrees)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
			{ "scale", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(TransformComponent, scale)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr },
		};

		static ScenePropertyDesc k_camera_properties[] =
		{
			{ "primary", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(CameraComponent, primary)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "reverse_z", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(CameraComponent, reverse_z)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "projection", ScenePropertyType::Enum, static_cast<uint32_t>(offsetof(CameraComponent, projection)), static_cast<uint32_t>(sizeof(CameraProjectionType)), "CameraProjectionType" },
			{ "fov_y_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, fov_y_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr, "Field Of View", "Vertical field of view in degrees.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 1.0f, 179.0f, true },
			{ "near_plane", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, near_plane)), static_cast<uint32_t>(sizeof(float)), nullptr, "Near Plane", "Camera near clip plane.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.001f, 1000.0f, true },
			{ "far_plane", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, far_plane)), static_cast<uint32_t>(sizeof(float)), nullptr, "Far Plane", "Camera far clip plane.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 1.0f, 100000.0f, true },
			{ "orthographic_height", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(CameraComponent, orthographic_height)), static_cast<uint32_t>(sizeof(float)), nullptr, "Orthographic Height", "Visible world height for orthographic projection.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.01f, 10000.0f, true },
		};

		static ScenePropertyDesc k_light_properties[] =
		{
			{ "type", ScenePropertyType::Enum, static_cast<uint32_t>(offsetof(LightComponent, type)), static_cast<uint32_t>(sizeof(LightType)), "LightType" },
			{ "color", ScenePropertyType::Vec3, static_cast<uint32_t>(offsetof(LightComponent, color)), static_cast<uint32_t>(sizeof(glm::vec3)), nullptr, "Color", "Light color multiplier.", ScenePropertyEditorHint::Color },
			{ "intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, intensity)), static_cast<uint32_t>(sizeof(float)), nullptr, "Intensity", "Light intensity.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.0f, 100.0f, true },
			{ "range", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, range)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "inner_cone_angle_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, inner_cone_angle_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "outer_cone_angle_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, outer_cone_angle_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "casts_shadow", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(LightComponent, casts_shadow)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "sunlight", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(LightComponent, sunlight)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "shadow_priority", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(LightComponent, shadow_priority)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
			{ "shadow_distance", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, shadow_distance)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "shadow_cascade_count", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(LightComponent, shadow_cascade_count)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
			{ "near_shadow_distance", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(LightComponent, near_shadow_distance)), static_cast<uint32_t>(sizeof(float)), nullptr },
		};

		static ScenePropertyDesc k_mesh_properties[] =
		{
			{ "asset_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(MeshComponent, asset_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr, "Asset Path", "Mesh asset file path.", ScenePropertyEditorHint::AssetPath, ScenePropertyAssetRefKind::Mesh },
			{ "mesh_index", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(MeshComponent, mesh_index)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
			{ "visible", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(MeshComponent, visible)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "mobility", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(MeshComponent, mobility)), static_cast<uint32_t>(sizeof(SceneMobility)), nullptr },
			{ "layer_mask", ScenePropertyType::UInt32, static_cast<uint32_t>(offsetof(MeshComponent, layer_mask)), static_cast<uint32_t>(sizeof(uint32_t)), nullptr },
		};

		static ScenePropertyDesc k_environment_properties[] =
		{
			{ "active", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, active)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "ibl_asset_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(EnvironmentComponent, ibl_asset_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr, "IBL Asset", "Image-based lighting asset path.", ScenePropertyEditorHint::AssetPath, ScenePropertyAssetRefKind::IBL },
			{ "source_texture_path", ScenePropertyType::String, static_cast<uint32_t>(offsetof(EnvironmentComponent, source_texture_path)), static_cast<uint32_t>(sizeof(std::string)), nullptr, "Source Texture", "Environment source texture path.", ScenePropertyEditorHint::AssetPath, ScenePropertyAssetRefKind::Texture },
			{ "intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, intensity)), static_cast<uint32_t>(sizeof(float)), nullptr, "Intensity", "Overall environment intensity.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.0f, 10.0f, true },
			{ "lighting_intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, lighting_intensity)), static_cast<uint32_t>(sizeof(float)), nullptr, "Lighting Intensity", "Diffuse/specular lighting contribution.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.0f, 10.0f, true },
			{ "background_intensity", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, background_intensity)), static_cast<uint32_t>(sizeof(float)), nullptr, "Background Intensity", "Skybox background contribution.", ScenePropertyEditorHint::Slider, ScenePropertyAssetRefKind::None, 0.0f, 10.0f, true },
			{ "rotation_degrees", ScenePropertyType::Float, static_cast<uint32_t>(offsetof(EnvironmentComponent, rotation_degrees)), static_cast<uint32_t>(sizeof(float)), nullptr },
			{ "visible_background", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, visible_background)), static_cast<uint32_t>(sizeof(bool)), nullptr },
			{ "affect_lighting", ScenePropertyType::Bool, static_cast<uint32_t>(offsetof(EnvironmentComponent, affect_lighting)), static_cast<uint32_t>(sizeof(bool)), nullptr },
		};

		static SceneComponentDesc k_scene_component_descs[] =
		{
			{ SceneComponentType::Name, "NameComponent", k_name_properties, static_cast<uint32_t>(std::size(k_name_properties)), static_cast<uint32_t>(sizeof(NameComponent)) },
			{ SceneComponentType::Transform, "TransformComponent", k_transform_properties, static_cast<uint32_t>(std::size(k_transform_properties)), static_cast<uint32_t>(sizeof(TransformComponent)) },
			{ SceneComponentType::Camera, "CameraComponent", k_camera_properties, static_cast<uint32_t>(std::size(k_camera_properties)), static_cast<uint32_t>(sizeof(CameraComponent)) },
			{ SceneComponentType::Light, "LightComponent", k_light_properties, static_cast<uint32_t>(std::size(k_light_properties)), static_cast<uint32_t>(sizeof(LightComponent)) },
			{ SceneComponentType::Mesh, "MeshComponent", k_mesh_properties, static_cast<uint32_t>(std::size(k_mesh_properties)), static_cast<uint32_t>(sizeof(MeshComponent)) },
			{ SceneComponentType::Environment, "EnvironmentComponent", k_environment_properties, static_cast<uint32_t>(std::size(k_environment_properties)), static_cast<uint32_t>(sizeof(EnvironmentComponent)) },
		};

		static auto to_json_vec3(const glm::vec3& value) -> json
		{
			return json::array({ value.x, value.y, value.z });
		}

		static auto from_json_vec3(const json& value, const glm::vec3& fallback) -> glm::vec3
		{
			if (!value.is_array() || value.size() != 3)
			{
				return fallback;
			}

			glm::vec3 result = fallback;
			result.x = value[0].get<float>();
			result.y = value[1].get<float>();
			result.z = value[2].get<float>();
			return result;
		}

		static auto serialize_material_overrides(const std::vector<MeshMaterialOverride>& overrides) -> json
		{
			json result = json::array();
			for (const MeshMaterialOverride& override_desc : overrides)
			{
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				result.push_back(
				{
					{ "material_slot", override_desc.material_slot },
					{ "material_path", override_desc.material_path },
				});
			}
			return result;
		}

		static auto deserialize_material_overrides(const json& value) -> std::vector<MeshMaterialOverride>
		{
			std::vector<MeshMaterialOverride> overrides{};
			if (!value.is_array())
			{
				return overrides;
			}

			overrides.reserve(value.size());
			for (const json& entry : value)
			{
				if (!entry.is_object())
				{
					continue;
				}

				MeshMaterialOverride override_desc{};
				override_desc.material_slot = entry.value("material_slot", k_invalid_material_slot);
				override_desc.material_path = entry.value("material_path", std::string{});
				if (override_desc.material_slot == k_invalid_material_slot || override_desc.material_path.empty())
				{
					continue;
				}

				overrides.push_back(std::move(override_desc));
			}

			return overrides;
		}

		template <typename TValue>
		static auto try_get_json_value(const json& object, const char* key, TValue& out_value) -> bool
		{
			if (!object.is_object())
			{
				return false;
			}

			const auto it = object.find(key);
			if (it == object.end() || it->is_null())
			{
				return false;
			}

			try
			{
				out_value = it->get<TValue>();
				return true;
			}
			catch (const std::exception& exception)
			{
				HLogWarning("SceneConfig field '{}' has invalid type: {}.", key, exception.what());
				return false;
			}
		}

		static auto try_get_json_vec3(const json& object, const char* key, glm::vec3& out_value) -> bool
		{
			const auto value_it = object.find(key);
			if (value_it == object.end() || !value_it->is_array() || value_it->size() != 3u)
			{
				return false;
			}

			try
			{
				out_value.x = (*value_it)[0].get<float>();
				out_value.y = (*value_it)[1].get<float>();
				out_value.z = (*value_it)[2].get<float>();
				return true;
			}
			catch (const std::exception& exception)
			{
				HLogWarning("SceneConfig field '{}' has invalid vec3 value: {}.", key, exception.what());
				return false;
			}
		}

		static auto deserialize_scene_render_config(const json& root) -> SceneRenderConfig
		{
			SceneRenderConfig config = make_default_scene_render_config();
			const auto scene_config_it = root.find("scene_config");
			if (scene_config_it == root.end() || !scene_config_it->is_object())
			{
				return config;
			}

			const json& scene_config = *scene_config_it;
			if (const auto ao_it = scene_config.find("ambient_occlusion"); ao_it != scene_config.end() && ao_it->is_object())
			{
				std::string mode{};
				if (try_get_json_value(*ao_it, "mode", mode))
				{
					AmbientOcclusionMode parsed = config.ambient_occlusion.mode;
					if (try_parse_ambient_occlusion_mode(mode, parsed))
					{
						config.ambient_occlusion.mode = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig ambient_occlusion.mode '{}' is invalid. Keeping default '{}'.",
							mode,
							ambient_occlusion_mode_name(config.ambient_occlusion.mode));
					}
				}

				std::string quality{};
				if (try_get_json_value(*ao_it, "quality", quality))
				{
					AmbientOcclusionQuality parsed = config.ambient_occlusion.quality;
					if (try_parse_ambient_occlusion_quality(quality, parsed))
					{
						config.ambient_occlusion.quality = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig ambient_occlusion.quality '{}' is invalid. Keeping default '{}'.",
							quality,
							ambient_occlusion_quality_name(config.ambient_occlusion.quality));
					}
				}

				try_get_json_value(*ao_it, "radius", config.ambient_occlusion.radius);
				try_get_json_value(*ao_it, "intensity", config.ambient_occlusion.intensity);
				try_get_json_value(*ao_it, "power", config.ambient_occlusion.power);
				try_get_json_value(*ao_it, "half_resolution", config.ambient_occlusion.half_resolution);
				try_get_json_value(*ao_it, "blur", config.ambient_occlusion.blur);
				try_get_json_value(*ao_it, "temporal", config.ambient_occlusion.temporal);
				try_get_json_value(*ao_it, "temporal_blend", config.ambient_occlusion.temporal_blend);
				try_get_json_value(*ao_it, "temporal_depth_threshold", config.ambient_occlusion.temporal_depth_threshold);
				try_get_json_value(*ao_it, "temporal_normal_threshold", config.ambient_occlusion.temporal_normal_threshold);
				config.ambient_occlusion =
					sanitize_ambient_occlusion_config(config.ambient_occlusion, make_default_ambient_occlusion_config());
			}

			if (const auto shadows_it = scene_config.find("directional_shadows"); shadows_it != scene_config.end() && shadows_it->is_object())
			{
				try_get_json_value(*shadows_it, "enabled", config.directional_shadows.enabled);
				try_get_json_value(*shadows_it, "default_cascade_count", config.directional_shadows.default_cascade_count);
				try_get_json_value(*shadows_it, "default_shadow_distance", config.directional_shadows.default_shadow_distance);
				try_get_json_value(*shadows_it, "near_shadow_distance", config.directional_shadows.near_shadow_distance);
				try_get_json_value(*shadows_it, "split_lambda", config.directional_shadows.split_lambda);
				try_get_json_value(*shadows_it, "near_cascade_resolution", config.directional_shadows.near_cascade_resolution);
				try_get_json_value(*shadows_it, "outer_cascade_resolution", config.directional_shadows.outer_cascade_resolution);
				try_get_json_value(*shadows_it, "dynamic_atlas_size", config.directional_shadows.dynamic_atlas_size);
				try_get_json_value(*shadows_it, "static_cache_atlas_size", config.directional_shadows.static_cache_atlas_size);
				try_get_json_value(*shadows_it, "static_cache_budget_mb", config.directional_shadows.static_cache_budget_mb);
				try_get_json_value(*shadows_it, "depth_bias", config.directional_shadows.depth_bias);
				try_get_json_value(*shadows_it, "normal_bias", config.directional_shadows.normal_bias);
				try_get_json_value(*shadows_it, "pcf_radius", config.directional_shadows.pcf_radius);
				config.directional_shadows =
					sanitize_directional_shadow_config(config.directional_shadows, make_default_directional_shadow_config());
			}

			if (const auto bloom_it = scene_config.find("bloom"); bloom_it != scene_config.end() && bloom_it->is_object())
			{
				try_get_json_value(*bloom_it, "enabled", config.bloom.enabled);

				std::string quality{};
				if (try_get_json_value(*bloom_it, "quality", quality))
				{
					BloomQuality parsed = config.bloom.quality;
					if (try_parse_bloom_quality(quality, parsed))
					{
						config.bloom.quality = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig bloom.quality '{}' is invalid. Keeping default '{}'.",
							quality,
							bloom_quality_name(config.bloom.quality));
					}
				}

				try_get_json_value(*bloom_it, "intensity", config.bloom.intensity);
				try_get_json_value(*bloom_it, "threshold", config.bloom.threshold);
				try_get_json_value(*bloom_it, "soft_knee", config.bloom.soft_knee);
				try_get_json_value(*bloom_it, "size_scale", config.bloom.size_scale);

				std::string debug_view{};
				if (try_get_json_value(*bloom_it, "debug_view", debug_view))
				{
					BloomDebugView parsed = config.bloom.debug_view;
					if (try_parse_bloom_debug_view(debug_view, parsed))
					{
						config.bloom.debug_view = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig bloom.debug_view '{}' is invalid. Keeping default '{}'.",
							debug_view,
							bloom_debug_view_name(config.bloom.debug_view));
					}
				}

				if (const auto stages_it = bloom_it->find("stages"); stages_it != bloom_it->end() && stages_it->is_array())
				{
					const size_t stage_count = std::min(config.bloom.stages.size(), stages_it->size());
					for (size_t index = 0; index < stage_count; ++index)
					{
						const json& stage_json = (*stages_it)[index];
						if (!stage_json.is_object())
						{
							continue;
						}
						try_get_json_value(stage_json, "size", config.bloom.stages[index].size);
						try_get_json_vec3(stage_json, "tint", config.bloom.stages[index].tint);
					}
				}

				config.bloom = sanitize_bloom_config(config.bloom, make_default_bloom_config());
			}

			if (const auto volumetric_it = scene_config.find("volumetric_lighting");
				volumetric_it != scene_config.end() && volumetric_it->is_object())
			{
				try_get_json_value(*volumetric_it, "enabled", config.volumetric_lighting.enabled);

				std::string quality{};
				if (try_get_json_value(*volumetric_it, "quality", quality))
				{
					VolumetricLightingQuality parsed = config.volumetric_lighting.quality;
					if (try_parse_volumetric_lighting_quality(quality, parsed))
					{
						config.volumetric_lighting.quality = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig volumetric_lighting.quality '{}' is invalid. Keeping default '{}'.",
							quality,
							volumetric_lighting_quality_name(config.volumetric_lighting.quality));
					}
				}

				try_get_json_value(*volumetric_it, "froxel_resolution_scale", config.volumetric_lighting.froxel_resolution_scale);
				try_get_json_value(*volumetric_it, "froxel_depth_slices", config.volumetric_lighting.froxel_depth_slices);
				try_get_json_value(*volumetric_it, "max_lights", config.volumetric_lighting.max_lights);
				try_get_json_value(*volumetric_it, "density", config.volumetric_lighting.density);
				try_get_json_value(*volumetric_it, "scattering_intensity", config.volumetric_lighting.scattering_intensity);
				try_get_json_value(*volumetric_it, "extinction_scale", config.volumetric_lighting.extinction_scale);
				try_get_json_value(*volumetric_it, "anisotropy", config.volumetric_lighting.anisotropy);
				try_get_json_value(*volumetric_it, "history", config.volumetric_lighting.history);
				try_get_json_value(*volumetric_it, "history_blend", config.volumetric_lighting.history_blend);
				try_get_json_value(*volumetric_it, "screen_space_fallback", config.volumetric_lighting.screen_space_fallback);

				std::string debug_view{};
				if (try_get_json_value(*volumetric_it, "debug_view", debug_view))
				{
					VolumetricLightingDebugView parsed = config.volumetric_lighting.debug_view;
					if (try_parse_volumetric_lighting_debug_view(debug_view, parsed))
					{
						config.volumetric_lighting.debug_view = parsed;
					}
					else
					{
						HLogWarning(
							"SceneConfig volumetric_lighting.debug_view '{}' is invalid. Keeping default '{}'.",
							debug_view,
							volumetric_lighting_debug_view_name(config.volumetric_lighting.debug_view));
					}
				}

				config.volumetric_lighting = sanitize_volumetric_lighting_config(
					config.volumetric_lighting,
					make_default_volumetric_lighting_config());
			}

			return config;
		}

		static auto serialize_scene_render_config(const SceneRenderConfig& config) -> json
		{
			return json{
				{ "ambient_occlusion", json{
					{ "mode", ambient_occlusion_mode_name(config.ambient_occlusion.mode) },
					{ "quality", ambient_occlusion_quality_name(config.ambient_occlusion.quality) },
					{ "radius", config.ambient_occlusion.radius },
					{ "intensity", config.ambient_occlusion.intensity },
					{ "power", config.ambient_occlusion.power },
					{ "half_resolution", config.ambient_occlusion.half_resolution },
					{ "blur", config.ambient_occlusion.blur },
					{ "temporal", config.ambient_occlusion.temporal },
					{ "temporal_blend", config.ambient_occlusion.temporal_blend },
					{ "temporal_depth_threshold", config.ambient_occlusion.temporal_depth_threshold },
					{ "temporal_normal_threshold", config.ambient_occlusion.temporal_normal_threshold },
				} },
				{ "directional_shadows", json{
					{ "enabled", config.directional_shadows.enabled },
					{ "default_cascade_count", config.directional_shadows.default_cascade_count },
					{ "default_shadow_distance", config.directional_shadows.default_shadow_distance },
					{ "near_shadow_distance", config.directional_shadows.near_shadow_distance },
					{ "split_lambda", config.directional_shadows.split_lambda },
					{ "near_cascade_resolution", config.directional_shadows.near_cascade_resolution },
					{ "outer_cascade_resolution", config.directional_shadows.outer_cascade_resolution },
					{ "dynamic_atlas_size", config.directional_shadows.dynamic_atlas_size },
					{ "static_cache_atlas_size", config.directional_shadows.static_cache_atlas_size },
					{ "static_cache_budget_mb", config.directional_shadows.static_cache_budget_mb },
					{ "depth_bias", config.directional_shadows.depth_bias },
					{ "normal_bias", config.directional_shadows.normal_bias },
					{ "pcf_radius", config.directional_shadows.pcf_radius },
				} },
				{ "bloom", json{
					{ "enabled", config.bloom.enabled },
					{ "quality", bloom_quality_name(config.bloom.quality) },
					{ "intensity", config.bloom.intensity },
					{ "threshold", config.bloom.threshold },
					{ "soft_knee", config.bloom.soft_knee },
					{ "size_scale", config.bloom.size_scale },
					{ "debug_view", bloom_debug_view_name(config.bloom.debug_view) },
					{ "stages", json::array({
						json{ { "size", config.bloom.stages[0].size }, { "tint", to_json_vec3(config.bloom.stages[0].tint) } },
						json{ { "size", config.bloom.stages[1].size }, { "tint", to_json_vec3(config.bloom.stages[1].tint) } },
						json{ { "size", config.bloom.stages[2].size }, { "tint", to_json_vec3(config.bloom.stages[2].tint) } },
						json{ { "size", config.bloom.stages[3].size }, { "tint", to_json_vec3(config.bloom.stages[3].tint) } },
						json{ { "size", config.bloom.stages[4].size }, { "tint", to_json_vec3(config.bloom.stages[4].tint) } },
						json{ { "size", config.bloom.stages[5].size }, { "tint", to_json_vec3(config.bloom.stages[5].tint) } }
					}) },
				} },
				{ "volumetric_lighting", json{
					{ "enabled", config.volumetric_lighting.enabled },
					{ "quality", volumetric_lighting_quality_name(config.volumetric_lighting.quality) },
					{ "froxel_resolution_scale", config.volumetric_lighting.froxel_resolution_scale },
					{ "froxel_depth_slices", config.volumetric_lighting.froxel_depth_slices },
					{ "max_lights", config.volumetric_lighting.max_lights },
					{ "density", config.volumetric_lighting.density },
					{ "scattering_intensity", config.volumetric_lighting.scattering_intensity },
					{ "extinction_scale", config.volumetric_lighting.extinction_scale },
					{ "anisotropy", config.volumetric_lighting.anisotropy },
					{ "history", config.volumetric_lighting.history },
					{ "history_blend", config.volumetric_lighting.history_blend },
					{ "screen_space_fallback", config.volumetric_lighting.screen_space_fallback },
					{ "debug_view", volumetric_lighting_debug_view_name(config.volumetric_lighting.debug_view) },
				} },
			};
		}

		static auto make_scene_error(std::string* out_error, std::string_view message) -> void
		{
			if (out_error)
			{
				*out_error = std::string(message);
			}
		}

		static auto allocate_scene_change_version() -> uint64_t
		{
			return g_scene_change_version_seed.fetch_add(1, std::memory_order_relaxed);
		}

		// editor begin 修改原因：Scene Change Event 辅助函数
		static auto notify_scene_change_event(SceneStorage& storage, const SceneChangeEvent& event) -> void
		{
			for (const auto& [id, callback] : storage.change_callbacks)
			{
				if (callback)
				{
					callback(event);
				}
			}
		}
		// editor end

		static auto mark_scene_storage_modified(SceneStorage& storage, SceneChangeKind kind = SceneChangeKind::DirtyStateChanged, EntityId entity_id = 0, SceneComponentType component_type = SceneComponentType::Name) -> void
		{
			storage.dirty = true;
			storage.change_version = allocate_scene_change_version();

			// editor begin 修改原因：触发 Scene Change Event
			SceneChangeEvent event{};
			event.kind = kind;
			event.entity_id = entity_id;
			event.component_type = component_type;
			event.change_version = storage.change_version;
			event.dirty = storage.dirty;
			notify_scene_change_event(storage, event);
			// editor end
		}

		static auto mark_scene_storage_clean(SceneStorage& storage) -> void
		{
			if (!storage.dirty)
			{
				return;
			}

			storage.dirty = false;
			storage.change_version = allocate_scene_change_version();

			SceneChangeEvent event{};
			event.kind = SceneChangeKind::DirtyStateChanged;
			event.change_version = storage.change_version;
			event.dirty = storage.dirty;
			notify_scene_change_event(storage, event);
		}

		template <typename TComponent>
		static constexpr auto scene_component_type_for() -> SceneComponentType
		{
			if constexpr (std::is_same_v<TComponent, NameComponent>)
			{
				return SceneComponentType::Name;
			}
			else if constexpr (std::is_same_v<TComponent, TransformComponent>)
			{
				return SceneComponentType::Transform;
			}
			else if constexpr (std::is_same_v<TComponent, CameraComponent>)
			{
				return SceneComponentType::Camera;
			}
			else if constexpr (std::is_same_v<TComponent, LightComponent>)
			{
				return SceneComponentType::Light;
			}
			else if constexpr (std::is_same_v<TComponent, MeshComponent>)
			{
				return SceneComponentType::Mesh;
			}
			else if constexpr (std::is_same_v<TComponent, EnvironmentComponent>)
			{
				return SceneComponentType::Environment;
			}
			else
			{
				return SceneComponentType::Name;
			}
		}

		static auto mark_entity_component_changed(SceneStorage& storage, EntityId entity_id, SceneComponentType component_type) -> void
		{
			storage.dirty = true;
			storage.change_version = allocate_scene_change_version();

			if (component_type == SceneComponentType::Mesh)
			{
				storage.render_primitive_version = allocate_scene_change_version();
				storage.render_transform_version = allocate_scene_change_version();
			}
			else if (component_type == SceneComponentType::Transform)
			{
				storage.render_transform_version = storage.change_version;
			}
			else if (component_type == SceneComponentType::Light)
			{
				storage.render_light_version = storage.change_version;
			}
			else if (component_type == SceneComponentType::Environment)
			{
				storage.render_environment_version = storage.change_version;
			}

			SceneChangeEvent event{};
			event.kind = SceneChangeKind::ComponentChanged;
			event.entity_id = entity_id;
			event.component_type = component_type;
			event.change_version = storage.change_version;
			event.dirty = storage.dirty;
			notify_scene_change_event(storage, event);
		}

		static auto transfer_scene_storage_preserve_subscribers(SceneStorage& dst, SceneStorage&& src, SceneChangeKind kind) -> void
		{
			auto callbacks = std::move(dst.change_callbacks);
			const uint32_t next_subscription_id = dst.next_subscription_id;
			dst = std::move(src);
			dst.change_callbacks = std::move(callbacks);
			dst.next_subscription_id = std::max(next_subscription_id, dst.next_subscription_id);
			dst.change_version = allocate_scene_change_version();

			SceneChangeEvent event{};
			event.kind = kind;
			event.change_version = dst.change_version;
			event.dirty = dst.dirty;
			notify_scene_change_event(dst, event);
		}

		static auto mark_scene_render_primitives_modified(SceneStorage& storage) -> void
		{
			mark_scene_storage_modified(storage);
			storage.render_primitive_version = allocate_scene_change_version();
			storage.render_transform_version = allocate_scene_change_version();
		}

		static auto mark_scene_render_transforms_modified(SceneStorage& storage) -> void
		{
			mark_scene_storage_modified(storage);
			storage.render_transform_version = allocate_scene_change_version();
		}

		static auto mark_scene_render_lights_modified(SceneStorage& storage) -> void
		{
			mark_scene_storage_modified(storage);
			storage.render_light_version = allocate_scene_change_version();
		}

		static auto mark_scene_render_environment_modified(SceneStorage& storage) -> void
		{
			mark_scene_storage_modified(storage);
			storage.render_environment_version = allocate_scene_change_version();
		}

		static auto matrix_to_transform_component(const glm::mat4& matrix) -> TransformComponent
		{
			TransformComponent component{};
			glm::vec3 scale{};
			glm::quat rotation{};
			glm::vec3 translation{};
			glm::vec3 skew{};
			glm::vec4 perspective{};
			if (glm::decompose(matrix, scale, rotation, translation, skew, perspective))
			{
				component.position = translation;
				component.scale = scale;
				component.rotation_euler_degrees = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
			}
			return component;
		}

		static auto transform_component_to_matrix(const TransformComponent& component) -> glm::mat4
		{
			const glm::mat4 translation = glm::translate(glm::mat4(1.0f), component.position);
			const glm::quat rotation = glm::quat(glm::radians(component.rotation_euler_degrees));
			const glm::mat4 rotation_matrix = glm::mat4_cast(rotation);
			const glm::mat4 scale = glm::scale(glm::mat4(1.0f), component.scale);
			return translation * rotation_matrix * scale;
		}

		static auto vector_is_finite(const glm::vec3& value) -> bool
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		static auto normalize_direction_or_zero(const glm::vec3& value) -> glm::vec3
		{
			if (!vector_is_finite(value))
			{
				return glm::vec3(0.0f);
			}

			const float length = glm::length(value);
			return length > 0.0001f ? value / length : glm::vec3(0.0f);
		}

		static auto rotate_environment_direction(const glm::vec3& direction, float rotation_degrees) -> glm::vec3
		{
			const float rotation_radians = glm::radians(rotation_degrees);
			const float c = std::cos(rotation_radians);
			const float s = std::sin(rotation_radians);
			return glm::vec3(
				c * direction.x - s * direction.z,
				direction.y,
				s * direction.x + c * direction.z);
		}

		static auto make_directional_light_transform(const glm::vec3& light_ray_direction_ws) -> TransformComponent
		{
			TransformComponent transform{};
			const glm::vec3 direction = normalize_direction_or_zero(light_ray_direction_ws);
			if (glm::length(direction) <= 0.0001f)
			{
				return transform;
			}

			const glm::vec3 up_seed =
				std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.98f ?
				glm::vec3(1.0f, 0.0f, 0.0f) :
				glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::quat rotation = glm::quatLookAtLH(direction, up_seed);
			transform.rotation_euler_degrees = glm::degrees(glm::eulerAngles(glm::normalize(rotation)));
			return transform;
		}

		static auto sanitize_light_component(LightComponent component) -> LightComponent
		{
			if (component.type != LightType::Directional)
			{
				component.sunlight = false;
			}
			return component;
		}

		static auto validate_single_directional_sunlight(const Scene& scene, std::string* out_error) -> bool
		{
			uint32_t sunlight_count = 0u;
			for (const Entity& entity : scene.get_entities())
			{
				if (!entity.is_valid() || !entity.has_light_component())
				{
					continue;
				}

				const LightComponent light = entity.get_light_component();
				if (light.type == LightType::Directional && light.sunlight)
				{
					++sunlight_count;
				}
			}

			if (sunlight_count > 1u)
			{
				make_scene_error(out_error, "Scene contains more than one directional sunlight.");
				return false;
			}
			return true;
		}

		static auto find_directional_sunlight_entity(const Scene& scene) -> Entity
		{
			for (const Entity& entity : scene.get_entities())
			{
				if (!entity.is_valid() || !entity.has_light_component())
				{
					continue;
				}

				const LightComponent light = entity.get_light_component();
				if (light.type == LightType::Directional && light.sunlight)
				{
					return entity;
				}
			}
			return {};
		}

		static auto find_environment_sun_light(Scene& scene) -> Entity
		{
			for (const Entity& entity : scene.get_entities())
			{
				if (!entity.is_valid() || entity.get_name() != k_environment_sun_light_name || !entity.has_light_component())
				{
					continue;
				}

				const LightComponent light = entity.get_light_component();
				if (light.type == LightType::Directional)
				{
					return entity;
				}
			}
			return {};
		}

		static auto ensure_environment_sun_light_from_metadata(Scene& scene, std::string* out_error) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			ASH_PROCESS_ERROR(scene.is_valid());

			if (find_directional_sunlight_entity(scene).is_valid())
			{
				return true;
			}

			SceneEnvironmentExtractionDesc environment{};
			if (!scene.extract_active_environment(environment))
			{
				return true;
			}
			if (environment.ibl_asset_path.empty())
			{
				return true;
			}

			const std::filesystem::path ibl_path = std::filesystem::path(environment.ibl_asset_path).lexically_normal();
			EnvironmentMapMetadata metadata{};
			std::string metadata_error{};
			if (!read_ashibl_metadata_file(ibl_path, metadata, &metadata_error))
			{
				HLogWarning(
					"Scene '{}': could not read environment sun metadata from '{}': {}",
					scene.get_name(),
					ibl_path.generic_string(),
					metadata_error.empty() ? "unknown error" : metadata_error);
				return true;
			}
			if (!metadata.dominant_light.valid)
			{
				return true;
			}

			const glm::vec3 sun_direction_ws = normalize_direction_or_zero(
				rotate_environment_direction(metadata.dominant_light.direction, environment.rotation_degrees));
			if (glm::length(sun_direction_ws) <= 0.0001f)
			{
				HLogWarning(
					"Scene '{}': ignored invalid environment sun direction from '{}'.",
					scene.get_name(),
					ibl_path.generic_string());
				return true;
			}

			const glm::vec3 light_ray_direction_ws = -sun_direction_ws;
			Entity sun_entity = find_environment_sun_light(scene);
			if (!sun_entity.is_valid())
			{
				sun_entity = scene.create_entity(k_environment_sun_light_name);
			}
			if (!sun_entity.is_valid())
			{
				make_scene_error(out_error, "Failed to create environment sun light entity.");
				ASH_PROCESS_ERROR(false);
			}

			const DirectionalShadowConfig& shadow_config = scene.get_render_config().directional_shadows;
			LightComponent sun_light{};
			sun_light.type = LightType::Directional;
			sun_light.sunlight = true;
			sun_light.color = glm::vec3(1.0f, 0.95f, 0.88f);
			sun_light.intensity = k_environment_sun_light_intensity;
			sun_light.casts_shadow = true;
			sun_light.shadow_priority = k_environment_sun_light_shadow_priority;
			sun_light.shadow_distance = shadow_config.default_shadow_distance;
			sun_light.shadow_cascade_count = shadow_config.default_cascade_count;
			sun_light.near_shadow_distance = shadow_config.near_shadow_distance;

			if (!sun_entity.set_transform_component(make_directional_light_transform(light_ray_direction_ws)) ||
				!sun_entity.set_light_component(sun_light))
			{
				make_scene_error(out_error, "Failed to configure environment sun light.");
				ASH_PROCESS_ERROR(false);
			}

			HLogInfo(
				"Scene '{}': created '{}' from '{}'. sun_direction_ws=({}, {}, {}) light_ray_direction_ws=({}, {}, {}).",
				scene.get_name(),
				k_environment_sun_light_name,
				ibl_path.generic_string(),
				sun_direction_ws.x,
				sun_direction_ws.y,
				sun_direction_ws.z,
				light_ray_direction_ws.x,
				light_ray_direction_ws.y,
				light_ray_direction_ws.z);
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}
	}

	class Entity::Impl
	{
	public:
		SceneStorage storage{};
	};

	class Scene::Impl : public Entity::Impl
	{
	};

	namespace
	{
		template <typename TComponent>
		static auto emplace_or_replace_entity_component(const std::shared_ptr<Scene::Impl>& impl, EntityId id, const TComponent& component) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			const entt::entity handle = find_handle(impl, id);
			ASH_PROCESS_ERROR(handle != entt::null);

			impl->storage.registry.emplace_or_replace<TComponent>(handle, component);
			mark_entity_component_changed(impl->storage, id, scene_component_type_for<TComponent>());
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		template <typename TComponent>
		static auto remove_entity_component_if_present(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> bool
		{
			ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
			const entt::entity handle = find_handle(impl, id);
			ASH_PROCESS_ERROR(handle != entt::null && impl->storage.registry.any_of<TComponent>(handle));

			impl->storage.registry.remove<TComponent>(handle);
			mark_entity_component_changed(impl->storage, id, scene_component_type_for<TComponent>());
			ASH_PROCESS_GUARD_RETURN_END(bResult, false);
		}

		static auto find_handle(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> entt::entity
		{
			if (!impl)
			{
				return entt::null;
			}
			const auto found = impl->storage.entities.find(id);
			return found != impl->storage.entities.end() ? found->second : entt::null;
		}

		static auto find_hierarchy(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> HierarchyComponent*
		{
			const entt::entity handle = find_handle(impl, id);
			if (handle == entt::null)
			{
				return nullptr;
			}
			return impl->storage.registry.try_get<HierarchyComponent>(handle);
		}

		static auto find_hierarchy_const(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> const HierarchyComponent*
		{
			const entt::entity handle = find_handle(impl, id);
			if (handle == entt::null)
			{
				return nullptr;
			}
			return impl->storage.registry.try_get<HierarchyComponent>(handle);
		}

		static auto get_entity_local_matrix(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> glm::mat4
		{
			const entt::entity handle = find_handle(impl, id);
			if (handle == entt::null)
			{
				return glm::mat4(1.0f);
			}

			const TransformComponent* transform = impl->storage.registry.try_get<TransformComponent>(handle);
			return transform ? transform_component_to_matrix(*transform) : glm::mat4(1.0f);
		}

		static auto get_entity_world_matrix(const std::shared_ptr<Scene::Impl>& impl, EntityId id) -> glm::mat4
		{
			glm::mat4 world = get_entity_local_matrix(impl, id);
			const HierarchyComponent* hierarchy = find_hierarchy_const(impl, id);
			EntityId parent = hierarchy ? hierarchy->parent : 0;
			while (parent != 0)
			{
				world = get_entity_local_matrix(impl, parent) * world;
				const HierarchyComponent* parent_hierarchy = find_hierarchy_const(impl, parent);
				parent = parent_hierarchy ? parent_hierarchy->parent : 0;
			}
			return world;
		}

		static auto is_scene_descendant(const std::shared_ptr<Scene::Impl>& impl, EntityId potential_parent, EntityId id) -> bool
		{
			const HierarchyComponent* hierarchy = find_hierarchy_const(impl, id);
			if (!hierarchy)
			{
				return false;
			}
			EntityId parent = hierarchy->parent;
			while (parent != 0)
			{
				if (parent == potential_parent)
				{
					return true;
				}
				const HierarchyComponent* parent_hierarchy = find_hierarchy_const(impl, parent);
				parent = parent_hierarchy ? parent_hierarchy->parent : 0;
			}
			return false;
		}

		static auto clamp_sibling_index(size_t sibling_count, uint32_t sibling_index) -> size_t
		{
			return sibling_index == k_scene_append_sibling_index
				? sibling_count
				: std::min<size_t>(sibling_count, sibling_index);
		}

		static auto erase_entity_id(std::vector<EntityId>& ids, EntityId id) -> bool
		{
			const auto it = std::find(ids.begin(), ids.end(), id);
			if (it == ids.end())
			{
				return false;
			}
			ids.erase(it);
			return true;
		}

		static auto find_sibling_ids(SceneStorage& storage, EntityId parent_id) -> std::vector<EntityId>*
		{
			if (parent_id == 0)
			{
				return &storage.root_order;
			}

			const auto parent_it = storage.entities.find(parent_id);
			if (parent_it == storage.entities.end())
			{
				return nullptr;
			}

			HierarchyComponent* parent_hierarchy = storage.registry.try_get<HierarchyComponent>(parent_it->second);
			return parent_hierarchy ? &parent_hierarchy->children : nullptr;
		}

		static auto detach_from_current_siblings(SceneStorage& storage, EntityId id) -> void
		{
			const auto found = storage.entities.find(id);
			if (found == storage.entities.end())
			{
				return;
			}

			HierarchyComponent* hierarchy = storage.registry.try_get<HierarchyComponent>(found->second);
			if (!hierarchy)
			{
				return;
			}

			if (std::vector<EntityId>* siblings = find_sibling_ids(storage, hierarchy->parent))
			{
				erase_entity_id(*siblings, id);
			}
			hierarchy->parent = 0;
		}

		static auto attach_to_parent(SceneStorage& storage, EntityId id, EntityId parent, uint32_t sibling_index) -> bool
		{
			const auto found = storage.entities.find(id);
			if (found == storage.entities.end())
			{
				return false;
			}

			HierarchyComponent* hierarchy = storage.registry.try_get<HierarchyComponent>(found->second);
			std::vector<EntityId>* siblings = find_sibling_ids(storage, parent);
			if (!hierarchy || !siblings)
			{
				return false;
			}

			hierarchy->parent = parent;
			const size_t insert_index = clamp_sibling_index(siblings->size(), sibling_index);
			siblings->insert(siblings->begin() + static_cast<ptrdiff_t>(insert_index), id);
			return true;
		}

		static auto create_entity_internal(const std::shared_ptr<Scene::Impl>& impl, EntityId explicit_id, std::string_view name) -> Entity
		{
			if (!impl)
			{
				return {};
			}

			EntityId id = explicit_id != 0 ? explicit_id : impl->storage.next_entity_id++;
			impl->storage.next_entity_id = std::max(impl->storage.next_entity_id, id + 1);
			if (impl->storage.entities.find(id) != impl->storage.entities.end())
			{
				return {};
			}

			const entt::entity handle = impl->storage.registry.create();
			impl->storage.registry.emplace<EntityIdComponent>(handle, EntityIdComponent{ id });
			impl->storage.registry.emplace<NameComponent>(handle, NameComponent{ name.empty() ? std::string("Entity") : std::string(name) });
			impl->storage.registry.emplace<TransformComponent>(handle, TransformComponent{});
			impl->storage.registry.emplace<HierarchyComponent>(handle, HierarchyComponent{});
			impl->storage.entities.emplace(id, handle);
			impl->storage.entity_order.push_back(id);
			impl->storage.root_order.push_back(id);
			return Entity(impl, id);
		}

		static auto destroy_entity_recursive(SceneStorage& storage, EntityId id) -> bool
		{
			const auto found = storage.entities.find(id);
			if (found == storage.entities.end())
			{
				return false;
			}

			const std::vector<EntityId> children = storage.registry.get<HierarchyComponent>(found->second).children;
			for (EntityId child_id : children)
			{
				destroy_entity_recursive(storage, child_id);
			}

			detach_from_current_siblings(storage, id);
			storage.entity_order.erase(std::remove(storage.entity_order.begin(), storage.entity_order.end(), id), storage.entity_order.end());
			storage.registry.destroy(found->second);
			storage.entities.erase(found);
			return true;
		}
	}

	Entity::Entity(std::shared_ptr<Impl> impl, EntityId id)
		: m_impl(std::move(impl)), m_id(id)
	{
	}

	bool Entity::is_valid() const
	{
		return find_handle(std::static_pointer_cast<Scene::Impl>(m_impl), m_id) != entt::null;
	}

	EntityId Entity::get_id() const
	{
		return is_valid() ? m_id : 0;
	}

	NameComponent Entity::get_name_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null ? impl->storage.registry.get<NameComponent>(handle) : NameComponent{};
	}

	bool Entity::set_name_component(const NameComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, component);
	}

	std::string Entity::get_name() const
	{
		return get_name_component().value;
	}

	bool Entity::set_name(std::string_view name)
	{
		NameComponent component = get_name_component();
		component.value = std::string(name);
		return set_name_component(component);
	}

	TransformComponent Entity::get_transform_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null ? impl->storage.registry.get<TransformComponent>(handle) : TransformComponent{};
	}

	bool Entity::set_transform_component(const TransformComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, component);
	}

	// editor begin 修改原因：为编辑器预览相机提供静默更新 Transform 的能力，避免每帧操作触发正式编辑副作用。
	bool Entity::set_transform_component_silent(const TransformComponent& component)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		ASH_PROCESS_ERROR(handle != entt::null);
		impl->storage.registry.emplace_or_replace<TransformComponent>(handle, component);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}
	// editor end

	bool Entity::has_camera_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<CameraComponent>(handle);
	}

	CameraComponent Entity::get_camera_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const CameraComponent* component = impl->storage.registry.try_get<CameraComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_camera_component(const CameraComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, component);
	}

	bool Entity::set_camera_component(const CameraComponent& component)
	{
		return add_camera_component(component);
	}

	bool Entity::remove_camera_component()
	{
		return remove_entity_component_if_present<CameraComponent>(std::static_pointer_cast<Scene::Impl>(m_impl), m_id);
	}

	bool Entity::has_light_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<LightComponent>(handle);
	}

	LightComponent Entity::get_light_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const LightComponent* component = impl->storage.registry.try_get<LightComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_light_component(const LightComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, sanitize_light_component(component));
	}

	bool Entity::set_light_component(const LightComponent& component)
	{
		return add_light_component(component);
	}

	bool Entity::remove_light_component()
	{
		return remove_entity_component_if_present<LightComponent>(std::static_pointer_cast<Scene::Impl>(m_impl), m_id);
	}

	bool Entity::has_mesh_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<MeshComponent>(handle);
	}

	MeshComponent Entity::get_mesh_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const MeshComponent* component = impl->storage.registry.try_get<MeshComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_mesh_component(const MeshComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, component);
	}

	bool Entity::set_mesh_component(const MeshComponent& component)
	{
		return add_mesh_component(component);
	}

	bool Entity::remove_mesh_component()
	{
		return remove_entity_component_if_present<MeshComponent>(std::static_pointer_cast<Scene::Impl>(m_impl), m_id);
	}

	bool Entity::has_environment_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		return handle != entt::null && impl->storage.registry.any_of<EnvironmentComponent>(handle);
	}

	EnvironmentComponent Entity::get_environment_component() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const entt::entity handle = find_handle(impl, m_id);
		if (handle != entt::null)
		{
			if (const EnvironmentComponent* component = impl->storage.registry.try_get<EnvironmentComponent>(handle))
			{
				return *component;
			}
		}
		return {};
	}

	bool Entity::add_environment_component(const EnvironmentComponent& component)
	{
		return emplace_or_replace_entity_component(std::static_pointer_cast<Scene::Impl>(m_impl), m_id, component);
	}

	bool Entity::set_environment_component(const EnvironmentComponent& component)
	{
		return add_environment_component(component);
	}

	bool Entity::remove_environment_component()
	{
		return remove_entity_component_if_present<EnvironmentComponent>(std::static_pointer_cast<Scene::Impl>(m_impl), m_id);
	}

	bool Entity::has_component(SceneComponentType type) const
	{
		switch (type)
		{
		case SceneComponentType::Name:
		case SceneComponentType::Transform:
			return is_valid();
		case SceneComponentType::Camera:
			return has_camera_component();
		case SceneComponentType::Light:
			return has_light_component();
		case SceneComponentType::Mesh:
			return has_mesh_component();
		case SceneComponentType::Environment:
			return has_environment_component();
		default:
			return false;
		}
	}

	std::vector<SceneComponentType> Entity::get_component_types() const
	{
		std::vector<SceneComponentType> result{};
		if (!is_valid())
		{
			return result;
		}

		result.push_back(SceneComponentType::Name);
		result.push_back(SceneComponentType::Transform);
		if (has_camera_component())
		{
			result.push_back(SceneComponentType::Camera);
		}
		if (has_light_component())
		{
			result.push_back(SceneComponentType::Light);
		}
		if (has_mesh_component())
		{
			result.push_back(SceneComponentType::Mesh);
		}
		if (has_environment_component())
		{
			result.push_back(SceneComponentType::Environment);
		}
		return result;
	}

	bool Entity::read_component(SceneComponentType type, void* out_component, size_t component_size) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, false, false);
		ASH_PROCESS_ERROR(out_component);

		bool handled = true;
		if (type == SceneComponentType::Name)
		{
			handled = component_size == sizeof(NameComponent);
			if (handled)
			{
				*static_cast<NameComponent*>(out_component) = get_name_component();
			}
		}
		else if (type == SceneComponentType::Transform)
		{
			handled = component_size == sizeof(TransformComponent);
			if (handled)
			{
				*static_cast<TransformComponent*>(out_component) = get_transform_component();
			}
		}
		else if (type == SceneComponentType::Camera)
		{
			handled = component_size == sizeof(CameraComponent) && has_camera_component();
			if (handled)
			{
				*static_cast<CameraComponent*>(out_component) = get_camera_component();
			}
		}
		else if (type == SceneComponentType::Light)
		{
			handled = component_size == sizeof(LightComponent) && has_light_component();
			if (handled)
			{
				*static_cast<LightComponent*>(out_component) = get_light_component();
			}
		}
		else if (type == SceneComponentType::Mesh)
		{
			handled = component_size == sizeof(MeshComponent) && has_mesh_component();
			if (handled)
			{
				*static_cast<MeshComponent*>(out_component) = get_mesh_component();
			}
		}
		else if (type == SceneComponentType::Environment)
		{
			handled = component_size == sizeof(EnvironmentComponent) && has_environment_component();
			if (handled)
			{
				*static_cast<EnvironmentComponent*>(out_component) = get_environment_component();
			}
		}
		else
		{
			handled = false;
		}

		ASH_PROCESS_ERROR(handled);
		bResult = true;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Entity::write_component(SceneComponentType type, const void* component_data, size_t component_size)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, false, false);
		ASH_PROCESS_ERROR(component_data);

		bool handled = true;
		if (type == SceneComponentType::Name)
		{
			handled = component_size == sizeof(NameComponent);
			if (handled)
			{
				bResult = set_name_component(*static_cast<const NameComponent*>(component_data));
			}
		}
		else if (type == SceneComponentType::Transform)
		{
			handled = component_size == sizeof(TransformComponent);
			if (handled)
			{
				bResult = set_transform_component(*static_cast<const TransformComponent*>(component_data));
			}
		}
		else if (type == SceneComponentType::Camera)
		{
			handled = component_size == sizeof(CameraComponent);
			if (handled)
			{
				bResult = set_camera_component(*static_cast<const CameraComponent*>(component_data));
			}
		}
		else if (type == SceneComponentType::Light)
		{
			handled = component_size == sizeof(LightComponent);
			if (handled)
			{
				bResult = set_light_component(*static_cast<const LightComponent*>(component_data));
			}
		}
		else if (type == SceneComponentType::Mesh)
		{
			handled = component_size == sizeof(MeshComponent);
			if (handled)
			{
				bResult = set_mesh_component(*static_cast<const MeshComponent*>(component_data));
			}
		}
		else if (type == SceneComponentType::Environment)
		{
			handled = component_size == sizeof(EnvironmentComponent);
			if (handled)
			{
				bResult = set_environment_component(*static_cast<const EnvironmentComponent*>(component_data));
			}
		}
		else
		{
			handled = false;
		}

		ASH_PROCESS_ERROR(handled);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	Entity Entity::get_parent() const
	{
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const HierarchyComponent* hierarchy = find_hierarchy_const(impl, m_id);
		return hierarchy && hierarchy->parent != 0 ? Entity(m_impl, hierarchy->parent) : Entity{};
	}

	std::vector<Entity> Entity::get_children() const
	{
		std::vector<Entity> result{};
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const HierarchyComponent* hierarchy = find_hierarchy_const(impl, m_id);
		if (!hierarchy)
		{
			return result;
		}

		result.reserve(hierarchy->children.size());
		for (EntityId child_id : hierarchy->children)
		{
			if (find_handle(impl, child_id) != entt::null)
			{
				result.emplace_back(m_impl, child_id);
			}
		}
		return result;
	}

	bool Entity::set_parent(const Entity& parent)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		const auto parent_impl = std::static_pointer_cast<Scene::Impl>(parent.m_impl);
		ASH_PROCESS_ERROR(is_valid() && parent.is_valid() && parent_impl == impl);

		Scene scene(impl);
		bResult = scene.reparent_entity(m_id, parent.get_id());
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Entity::clear_parent()
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		const auto impl = std::static_pointer_cast<Scene::Impl>(m_impl);
		ASH_PROCESS_ERROR(is_valid());

		Scene scene(impl);
		bResult = scene.reparent_entity(m_id, 0);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	Entity Entity::create_child(std::string_view name)
	{
		if (!is_valid())
		{
			return {};
		}
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.create_entity(name, *this);
	}

	bool Entity::destroy()
	{
		Scene scene(std::static_pointer_cast<Scene::Impl>(m_impl));
		return scene.destroy_entity(m_id);
	}

	Scene::Scene(std::shared_ptr<Impl> impl)
		: m_impl(std::move(impl))
	{
	}

	Scene Scene::create(std::string_view name)
	{
		auto impl = std::make_shared<Impl>();
		impl->storage.name = name.empty() ? "Untitled Scene" : std::string(name);
		impl->storage.change_version = allocate_scene_change_version();
		impl->storage.render_config = make_default_scene_render_config();
		impl->storage.render_config_version = impl->storage.change_version;
		return Scene(std::move(impl));
	}

	Scene Scene::load_from_file(const std::filesystem::path& path, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(Scene, result, Scene{}, Scene{});

		std::ifstream input(path);
		if (!input.is_open())
		{
			make_scene_error(out_error, "Failed to open scene file.");
			ASH_PROCESS_ERROR(false);
		}

		json root{};
		try
		{
			input >> root;
		}
		catch (const std::exception& exception)
		{
			make_scene_error(out_error, exception.what());
			ASH_PROCESS_ERROR(false);
		}

		Scene scene = Scene::create(root.value("name", std::string("Untitled Scene")));
		scene.m_impl->storage.source_path = path;
		const uint32_t version = root.value("version", k_scene_file_version);
		if (version > k_scene_file_version)
		{
			make_scene_error(out_error, "Scene file version is newer than this runtime supports.");
			ASH_PROCESS_ERROR(false);
		}
		scene.m_impl->storage.render_config = deserialize_scene_render_config(root);

		const json entities_json = root.value("entities", json::array());
		if (!entities_json.is_array())
		{
			make_scene_error(out_error, "Scene file contains an invalid entity list.");
			ASH_PROCESS_ERROR(false);
		}

		std::vector<std::pair<EntityId, EntityId>> desired_parents{};
		EntityId max_id = 0;
		for (const json& entity_json : entities_json)
		{
			const EntityId id = entity_json.value("id", static_cast<EntityId>(0));
			if (id == 0)
			{
				continue;
			}

			Entity entity = create_entity_internal(scene.m_impl, id, entity_json.value("name", std::string("Entity")));
			if (!entity.is_valid())
			{
				continue;
			}

			desired_parents.emplace_back(id, entity_json.value("parent", static_cast<EntityId>(0)));
			max_id = std::max(max_id, id);

			const json transform_json = entity_json.value("transform", json::object());
			TransformComponent transform = entity.get_transform_component();
			transform.position = from_json_vec3(transform_json.value("position", json::array()), transform.position);
			transform.rotation_euler_degrees = from_json_vec3(transform_json.value("rotation_euler_degrees", json::array()), transform.rotation_euler_degrees);
			transform.scale = from_json_vec3(transform_json.value("scale", json::array({ 1.0f, 1.0f, 1.0f })), transform.scale);
			entity.set_transform_component(transform);

			if (entity_json.contains("camera"))
			{
				const json& camera_json = entity_json["camera"];
				CameraComponent camera{};
				camera.primary = camera_json.value("primary", camera.primary);
				camera.reverse_z = camera_json.value("reverse_z", camera.reverse_z);
				camera.projection = static_cast<CameraProjectionType>(camera_json.value("projection", static_cast<int32_t>(camera.projection)));
				camera.fov_y_degrees = camera_json.value("fov_y_degrees", camera.fov_y_degrees);
				camera.near_plane = camera_json.value("near_plane", camera.near_plane);
				camera.far_plane = camera_json.value("far_plane", camera.far_plane);
				camera.orthographic_height = camera_json.value("orthographic_height", camera.orthographic_height);
				entity.add_camera_component(camera);
			}

			if (entity_json.contains("light"))
			{
				const json& light_json = entity_json["light"];
				LightComponent light{};
				light.type = static_cast<LightType>(light_json.value("type", static_cast<int32_t>(light.type)));
				light.color = from_json_vec3(light_json.value("color", json::array()), light.color);
				light.intensity = light_json.value("intensity", light.intensity);
				light.range = light_json.value("range", light.range);
				light.inner_cone_angle_degrees = light_json.value("inner_cone_angle_degrees", light.inner_cone_angle_degrees);
				light.outer_cone_angle_degrees = light_json.value("outer_cone_angle_degrees", light.outer_cone_angle_degrees);
				light.casts_shadow = light_json.value("casts_shadow", light.casts_shadow);
				light.sunlight = light_json.value("sunlight", light.sunlight);
				light.shadow_priority = light_json.value("shadow_priority", light.shadow_priority);
				light.shadow_distance = light_json.value("shadow_distance", light.shadow_distance);
				light.shadow_cascade_count = light_json.value("shadow_cascade_count", light.shadow_cascade_count);
				light.near_shadow_distance = light_json.value("near_shadow_distance", light.near_shadow_distance);
				entity.add_light_component(sanitize_light_component(light));
			}

			if (entity_json.contains("mesh"))
			{
				const json& mesh_json = entity_json["mesh"];
				MeshComponent mesh{};
				mesh.asset_path = mesh_json.value("asset_path", std::string{});
				mesh.mesh_index = mesh_json.value("mesh_index", 0u);
				mesh.material_overrides = deserialize_material_overrides(mesh_json.value("material_overrides", json::array()));
				mesh.visible = mesh_json.value("visible", true);
				mesh.mobility = static_cast<SceneMobility>(mesh_json.value("mobility", static_cast<uint32_t>(mesh.mobility)));
				mesh.layer_mask = mesh_json.value("layer_mask", mesh.layer_mask);
				entity.add_mesh_component(mesh);
			}

			if (entity_json.contains("environment"))
			{
				const json& environment_json = entity_json["environment"];
				EnvironmentComponent environment{};
				environment.active = environment_json.value("active", environment.active);
				environment.ibl_asset_path = environment_json.value("ibl_asset_path", std::string{});
				environment.source_texture_path = environment_json.value("source_texture_path", std::string{});
				environment.intensity = environment_json.value("intensity", environment.intensity);
				environment.lighting_intensity = environment_json.value("lighting_intensity", environment.lighting_intensity);
				environment.background_intensity = environment_json.value("background_intensity", environment.background_intensity);
				environment.rotation_degrees = environment_json.value("rotation_degrees", environment.rotation_degrees);
				environment.visible_background = environment_json.value("visible_background", environment.visible_background);
				environment.affect_lighting = environment_json.value("affect_lighting", environment.affect_lighting);
				entity.add_environment_component(environment);
			}
		}

		const EntityId saved_next_entity_id = root.value("next_entity_id", static_cast<EntityId>(0));
		scene.m_impl->storage.next_entity_id = std::max(scene.m_impl->storage.next_entity_id, max_id + 1);
		if (saved_next_entity_id > 0)
		{
			scene.m_impl->storage.next_entity_id = std::max(scene.m_impl->storage.next_entity_id, saved_next_entity_id);
		}
		bool hierarchy_valid = true;
		for (const auto& [id, parent] : desired_parents)
		{
			if (parent != 0 && !scene.reparent_entity(id, parent))
			{
				make_scene_error(out_error, "Scene file contains an invalid hierarchy.");
				hierarchy_valid = false;
				break;
			}
		}
		ASH_PROCESS_ERROR(hierarchy_valid);
		ASH_PROCESS_ERROR(ensure_environment_sun_light_from_metadata(scene, out_error));
		ASH_PROCESS_ERROR(validate_single_directional_sunlight(scene, out_error));

		scene.m_impl->storage.dirty = false;
		scene.m_impl->storage.change_version = allocate_scene_change_version();
		scene.m_impl->storage.render_config_version = scene.m_impl->storage.change_version;
		if (out_error)
		{
			out_error->clear();
		}
		result = std::move(scene);
		ASH_PROCESS_GUARD_RETURN_END(result, Scene{});
	}

	bool Scene::is_valid() const
	{
		return m_impl != nullptr;
	}

	const std::string& Scene::get_name() const
	{
		static const std::string k_empty_name{};
		return m_impl ? m_impl->storage.name : k_empty_name;
	}

	void Scene::set_name(std::string_view name)
	{
		if (!m_impl)
		{
			return;
		}
		m_impl->storage.name = name.empty() ? "Untitled Scene" : std::string(name);
		mark_scene_storage_modified(m_impl->storage);
	}

	Entity Scene::create_entity(std::string_view name)
	{
		return create_entity(name, Entity{});
	}

	Entity Scene::create_entity(std::string_view name, const Entity& parent)
	{
		return create_entity(name, parent, k_scene_append_sibling_index);
	}

	Entity Scene::create_entity(std::string_view name, const Entity& parent, uint32_t sibling_index)
	{
		if (!m_impl)
		{
			return {};
		}

		Entity entity = create_entity_internal(m_impl, 0, name);
		if (entity.is_valid() && parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl)
		{
			reparent_entity(entity.get_id(), parent.get_id(), sibling_index);
		}
		else if (entity.is_valid() && sibling_index != k_scene_append_sibling_index)
		{
			reparent_entity(entity.get_id(), 0, sibling_index);
		}
		// editor begin 修改原因：触发 EntityAdded 事件
		mark_scene_storage_modified(m_impl->storage, SceneChangeKind::EntityAdded, entity.get_id());
		// editor end
		mark_scene_render_primitives_modified(m_impl->storage);
		return entity;
	}

	Entity Scene::create_entity_with_id(EntityId explicit_id, std::string_view name)
	{
		return create_entity_with_id(explicit_id, name, Entity{});
	}

	Entity Scene::create_entity_with_id(EntityId explicit_id, std::string_view name, const Entity& parent)
	{
		return create_entity_with_id(explicit_id, name, parent, k_scene_append_sibling_index);
	}

	Entity Scene::create_entity_with_id(EntityId explicit_id, std::string_view name, const Entity& parent, uint32_t sibling_index)
	{
		if (!m_impl || explicit_id == 0)
		{
			return {};
		}

		Entity entity = create_entity_internal(m_impl, explicit_id, name);
		if (entity.is_valid() && parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl)
		{
			reparent_entity(entity.get_id(), parent.get_id(), sibling_index);
		}
		else if (entity.is_valid() && sibling_index != k_scene_append_sibling_index)
		{
			reparent_entity(entity.get_id(), 0, sibling_index);
		}
		// editor begin 修改原因：触发 EntityAdded 事件
		mark_scene_storage_modified(m_impl->storage, SceneChangeKind::EntityAdded, entity.get_id());
		// editor end
		mark_scene_render_primitives_modified(m_impl->storage);
		return entity;
	}

	// editor begin 修改原因：Scene Reload / Replace 语义
	bool Scene::reload_from_file(const std::filesystem::path& path, std::string* out_error)
	{
		Scene loaded = load_from_file(path, out_error);
		if (!loaded.is_valid())
		{
			return false;
		}

		if (!m_impl)
		{
			m_impl = std::move(loaded.m_impl);
			if (is_valid())
			{
				notify_change_event(
				{
					SceneChangeKind::SceneReloaded,
					0,
					SceneComponentType::Name,
					m_impl->storage.change_version,
					m_impl->storage.dirty
				});
			}
			return is_valid();
		}

		transfer_scene_storage_preserve_subscribers(m_impl->storage, std::move(loaded.m_impl->storage), SceneChangeKind::SceneReloaded);
		loaded.m_impl.reset();
		return true;
	}

	void Scene::replace_contents(Scene&& other)
	{
		if (!other.m_impl)
		{
			return;
		}

		if (!m_impl)
		{
			m_impl = std::move(other.m_impl);
			notify_change_event(
			{
				SceneChangeKind::SceneReplaced,
				0,
				SceneComponentType::Name,
				m_impl->storage.change_version,
				m_impl->storage.dirty
			});
			return;
		}

		transfer_scene_storage_preserve_subscribers(m_impl->storage, std::move(other.m_impl->storage), SceneChangeKind::SceneReplaced);
		other.m_impl.reset();
	}
	// editor end

	bool Scene::destroy_entity(EntityId id)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl);

		bResult = destroy_entity_recursive(m_impl->storage, id);
		if (bResult)
		{
			// editor begin 修改原因：触发 EntityRemoved 事件
			mark_scene_storage_modified(m_impl->storage, SceneChangeKind::EntityRemoved, id);
			// editor end
			mark_scene_render_primitives_modified(m_impl->storage);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Scene::reparent_entity(EntityId id, EntityId new_parent_id)
	{
		return reparent_entity(id, new_parent_id, k_scene_append_sibling_index);
	}

	bool Scene::reparent_entity(EntityId id, EntityId new_parent_id, uint32_t sibling_index)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl && id != 0 && id != new_parent_id);
		ASH_PROCESS_ERROR(find_handle(m_impl, id) != entt::null);
		ASH_PROCESS_ERROR(new_parent_id == 0 || (find_handle(m_impl, new_parent_id) != entt::null && !is_scene_descendant(m_impl, id, new_parent_id)));

		detach_from_current_siblings(m_impl->storage, id);
		if (!attach_to_parent(m_impl->storage, id, new_parent_id, sibling_index))
		{
			ASH_PROCESS_ERROR(false);
		}
		// editor begin 修改原因：触发 HierarchyChanged 事件
		mark_scene_storage_modified(m_impl->storage, SceneChangeKind::HierarchyChanged, id);
		// editor end
		mark_scene_render_transforms_modified(m_impl->storage);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	uint32_t Scene::get_entity_sibling_index(EntityId id) const
	{
		if (!m_impl || id == 0)
		{
			return 0;
		}

		const HierarchyComponent* hierarchy = find_hierarchy_const(m_impl, id);
		if (!hierarchy)
		{
			return 0;
		}

		const std::vector<EntityId>* siblings =
			hierarchy->parent == 0
			? &m_impl->storage.root_order
			: [&]() -> const std::vector<EntityId>* {
				const HierarchyComponent* parent_hierarchy = find_hierarchy_const(m_impl, hierarchy->parent);
				return parent_hierarchy ? &parent_hierarchy->children : nullptr;
			}();
		if (!siblings)
		{
			return 0;
		}

		const auto it = std::find(siblings->begin(), siblings->end(), id);
		return it == siblings->end()
			? 0u
			: static_cast<uint32_t>(std::distance(siblings->begin(), it));
	}

	Entity Scene::find_entity(EntityId id) const
	{
		return find_handle(m_impl, id) != entt::null ? Entity(m_impl, id) : Entity{};
	}

	std::vector<Entity> Scene::get_entities() const
	{
		std::vector<Entity> result{};
		if (!m_impl)
		{
			return result;
		}
		result.reserve(m_impl->storage.entity_order.size());
		for (EntityId id : m_impl->storage.entity_order)
		{
			if (find_handle(m_impl, id) != entt::null)
			{
				result.emplace_back(m_impl, id);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_root_entities() const
	{
		std::vector<Entity> result{};
		if (!m_impl)
		{
			return result;
		}
		result.reserve(m_impl->storage.root_order.size());
		for (EntityId id : m_impl->storage.root_order)
		{
			if (find_handle(m_impl, id) != entt::null)
			{
				result.emplace_back(m_impl, id);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_entities_with_component(SceneComponentType type) const
	{
		std::vector<Entity> result{};
		for (const Entity& entity : get_entities())
		{
			if (entity.has_component(type))
			{
				result.push_back(entity);
			}
		}
		return result;
	}

	std::vector<Entity> Scene::get_entities_with_components(const std::vector<SceneComponentType>& required_types) const
	{
		std::vector<Entity> result{};
		for (const Entity& entity : get_entities())
		{
			bool matches = true;
			for (SceneComponentType type : required_types)
			{
				if (!entity.has_component(type))
				{
					matches = false;
					break;
				}
			}
			if (matches)
			{
				result.push_back(entity);
			}
		}
		return result;
	}

	uint32_t Scene::get_entity_count() const
	{
		return m_impl ? static_cast<uint32_t>(m_impl->storage.entities.size()) : 0;
	}

	glm::mat4 Scene::get_entity_local_transform(EntityId id) const
	{
		return get_entity_local_matrix(m_impl, id);
	}

	glm::mat4 Scene::get_entity_world_transform(EntityId id) const
	{
		return get_entity_world_matrix(m_impl, id);
	}

	std::vector<SceneMeshExtractionDesc> Scene::extract_mesh_entities() const
	{
		std::vector<SceneMeshExtractionDesc> result{};
		if (!m_impl)
		{
			return result;
		}

		for (EntityId id : m_impl->storage.entity_order)
		{
			const entt::entity handle = find_handle(m_impl, id);
			if (handle == entt::null)
			{
				continue;
			}

			const MeshComponent* mesh = m_impl->storage.registry.try_get<MeshComponent>(handle);
			// editor begin 修改原因：编辑器预览/临时实体可能有空 MeshComponent，提取渲染 mesh 时直接跳过避免后续资源查询失败。
			if (!mesh || mesh->asset_path.empty())
			{
				continue;
			}
			// editor end

			SceneMeshExtractionDesc desc{};
			desc.entity_id = id;
			desc.asset_path = mesh->asset_path;
			desc.mesh_index = mesh->mesh_index;
			desc.material_overrides = mesh->material_overrides;
			desc.visible = mesh->visible;
			desc.mobility = mesh->mobility;
			desc.layer_mask = mesh->layer_mask;
			desc.world_transform = get_entity_world_matrix(m_impl, id);
			result.push_back(std::move(desc));
		}

		return result;
	}

	std::vector<SceneMeshExtractionDesc> Scene::extract_visible_mesh_entities() const
	{
		std::vector<SceneMeshExtractionDesc> result{};
		for (const SceneMeshExtractionDesc& desc : extract_mesh_entities())
		{
			if (desc.visible)
			{
				result.push_back(desc);
			}
		}
		return result;
	}

	std::vector<SceneLightExtractionDesc> Scene::extract_light_entities() const
	{
		std::vector<SceneLightExtractionDesc> result{};
		if (!m_impl)
		{
			return result;
		}

		for (EntityId id : m_impl->storage.entity_order)
		{
			const entt::entity handle = find_handle(m_impl, id);
			if (handle == entt::null)
			{
				continue;
			}

			const LightComponent* light = m_impl->storage.registry.try_get<LightComponent>(handle);
			if (!light)
			{
				continue;
			}

			SceneLightExtractionDesc desc{};
			desc.entity_id = id;
			desc.light = *light;
			desc.world_transform = get_entity_world_matrix(m_impl, id);
			result.push_back(std::move(desc));
		}

		return result;
	}

	bool Scene::extract_active_environment(SceneEnvironmentExtractionDesc& out_environment) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_environment = {};
		ASH_PROCESS_ERROR(is_valid());

		bool found_active = false;
		bool warned_multiple = false;
		for (EntityId id : m_impl->storage.entity_order)
		{
			Entity entity = find_entity(id);
			if (!entity.is_valid() || !entity.has_environment_component())
			{
				continue;
			}

			const EnvironmentComponent component = entity.get_environment_component();
			if (!component.active)
			{
				continue;
			}

			if (found_active)
			{
				if (!warned_multiple)
				{
					HLogWarning("Scene '{}': multiple active EnvironmentComponent entries found; using the first active environment.", get_name());
					warned_multiple = true;
				}
				continue;
			}

			out_environment.entity_id = id;
			out_environment.ibl_asset_path = component.ibl_asset_path;
			out_environment.source_texture_path = component.source_texture_path;
			out_environment.intensity = component.intensity;
			out_environment.lighting_intensity = component.lighting_intensity;
			out_environment.background_intensity = component.background_intensity;
			out_environment.rotation_degrees = component.rotation_degrees;
			out_environment.visible_background = component.visible_background;
			out_environment.affect_lighting = component.affect_lighting;
			found_active = true;
		}

		bResult = found_active;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool Scene::try_get_mesh_local_bounds(AssetDatabase& database, const MeshComponent& mesh_component, SceneMeshBounds& out_bounds) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_bounds = {};
		ASH_PROCESS_ERROR(database.is_valid());
		ASH_PROCESS_ERROR(!mesh_component.asset_path.empty());

		std::shared_ptr<const Model> model{};
		ASH_PROCESS_ERROR(database.load_model_by_path(mesh_component.asset_path, model));
		ASH_PROCESS_ERROR(model);

		glm::vec3 bounds_min{};
		glm::vec3 bounds_max{};
		ASH_PROCESS_ERROR(try_get_model_mesh_bounds(*model, mesh_component.mesh_index, bounds_min, bounds_max));
		out_bounds.is_valid = true;
		out_bounds.local_min = bounds_min;
		out_bounds.local_max = bounds_max;
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	Entity Scene::instantiate_model(const Model& model, const Entity& parent, std::string_view root_name_override)
	{
		if (!m_impl || !model.is_valid())
		{
			return {};
		}

		const Entity resolved_parent = parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl ? parent : Entity{};
		if (model.nodes.empty())
		{
			Entity root = create_entity(root_name_override.empty() ? (model.name.empty() ? "Model" : model.name) : root_name_override, resolved_parent);
			if (model.meshes.size() == 1)
			{
				MeshComponent mesh{};
				mesh.asset_path = model.source_path.generic_string();
				mesh.mesh_index = 0;
				root.add_mesh_component(mesh);
			}
			else
			{
				for (uint32_t mesh_index = 0; mesh_index < model.meshes.size(); ++mesh_index)
				{
					Entity child = create_entity(model.meshes[mesh_index].name.empty() ? ("Mesh_" + std::to_string(mesh_index)) : model.meshes[mesh_index].name, root);
					MeshComponent mesh{};
					mesh.asset_path = model.source_path.generic_string();
					mesh.mesh_index = mesh_index;
					child.add_mesh_component(mesh);
				}
			}
			return root;
		}

		auto spawn_model_node = [&](auto&& self, uint32_t node_index, const Entity& parent_entity, std::string_view override_name) -> Entity
		{
			const ModelNode& node = model.nodes[node_index];
			const std::string entity_name = override_name.empty()
				? (node.name.empty() ? std::string("Entity") : node.name)
				: std::string(override_name);
			Entity entity = create_entity(entity_name, parent_entity);
			entity.set_transform_component(matrix_to_transform_component(node.local_transform));
			if (node.mesh_index >= 0)
			{
				MeshComponent mesh{};
				mesh.asset_path = model.source_path.generic_string();
				mesh.mesh_index = static_cast<uint32_t>(node.mesh_index);
				entity.add_mesh_component(mesh);
			}
			for (uint32_t child_index : node.children)
			{
				self(self, child_index, entity, {});
			}
			return entity;
		};

		if (model.root_nodes.size() == 1)
		{
			return spawn_model_node(spawn_model_node, model.root_nodes.front(), resolved_parent, root_name_override);
		}

		Entity synthetic_root = create_entity(root_name_override.empty() ? (model.name.empty() ? "Model" : model.name) : root_name_override, resolved_parent);
		for (uint32_t root_node : model.root_nodes)
		{
			spawn_model_node(spawn_model_node, root_node, synthetic_root, {});
		}
		return synthetic_root;
	}

	Entity Scene::instantiate_ashasset(const AshAsset& asset, const Entity& parent, std::string_view root_name_override)
	{
		if (!m_impl || !asset.is_valid())
		{
			return {};
		}

		const Entity resolved_parent = parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl ? parent : Entity{};
		auto spawn_asset_node = [&](auto&& self, uint32_t node_index, const Entity& parent_entity, std::string_view override_name) -> Entity
		{
			const AshAssetNode& node = asset.nodes[node_index];
			const std::string entity_name = override_name.empty()
				? (node.name.empty() ? std::string("Entity") : node.name)
				: std::string(override_name);
			Entity entity = create_entity(entity_name, parent_entity);
			entity.set_transform_component(node.transform);
			if (node.camera.has_value())
			{
				entity.add_camera_component(node.camera.value());
			}
			if (node.light.has_value())
			{
				entity.add_light_component(node.light.value());
			}
			if (node.mesh.has_value())
			{
				entity.add_mesh_component(node.mesh.value());
			}
			if (node.environment.has_value())
			{
				entity.add_environment_component(node.environment.value());
			}
			for (uint32_t child_index : node.children)
			{
				self(self, child_index, entity, {});
			}
			return entity;
		};

		if (asset.root_nodes.size() == 1)
		{
			return spawn_asset_node(spawn_asset_node, asset.root_nodes.front(), resolved_parent, root_name_override);
		}

		Entity synthetic_root = create_entity(root_name_override.empty() ? (asset.name.empty() ? "AshAsset" : asset.name) : root_name_override, resolved_parent);
		for (uint32_t root_node : asset.root_nodes)
		{
			spawn_asset_node(spawn_asset_node, root_node, synthetic_root, {});
		}
		return synthetic_root;
	}

	Entity Scene::instantiate_mesh(const std::shared_ptr<const Mesh>& mesh, const std::filesystem::path& asset_path, const Entity& parent, std::string_view root_name_override)
	{
		ASH_PROCESS_GUARD_RETURN(Entity, result, Entity{}, Entity{});
		ASH_PROCESS_ERROR(mesh != nullptr);
		ASH_PROCESS_ERROR(m_impl != nullptr);

		const Entity resolved_parent =
			parent.is_valid() && std::static_pointer_cast<Scene::Impl>(parent.m_impl) == m_impl ? parent : Entity{};

		Entity entity = create_entity(
			root_name_override.empty() ? (mesh->name.empty() ? "Mesh" : mesh->name) : std::string(root_name_override),
			resolved_parent);
		ASH_PROCESS_ERROR(entity.is_valid());

		MeshComponent mesh_component{};
		mesh_component.asset_path = asset_path.string();
		mesh_component.mesh_index = 0;
		mesh_component.visible = true;
		ASH_PROCESS_ERROR(entity.add_mesh_component(mesh_component));

		result = entity;
		ASH_PROCESS_GUARD_RETURN_END(result, Entity{});
	}

	Entity Scene::instantiate_asset(AssetDatabase& database, const std::filesystem::path& path, const Entity& parent)
	{
		ASH_PROCESS_GUARD_RETURN(Entity, result, Entity{}, Entity{});

		// editor begin 修改原因：使用 AssetDatabase 确定资源类型，支持 Mesh 实例化
		if (const AssetInfo* asset_info = database.find_asset_by_path(path); asset_info != nullptr)
		{
			if (asset_info->type == AssetType::Mesh)
			{
				std::shared_ptr<const Mesh> mesh{};
				ASH_PROCESS_ERROR(database.load_mesh_by_path(path, mesh));
				ASH_PROCESS_ERROR(mesh != nullptr);
				result = instantiate_mesh(mesh, path, parent);
			}
			else if (asset_info->type == AssetType::Prefab)
			{
				std::shared_ptr<const AshAsset> asset{};
				ASH_PROCESS_ERROR(database.load_ashasset_by_path(path, asset));
				result = instantiate_ashasset(*asset, parent);
			}
			else if (asset_info->type == AssetType::Model)
			{
				std::shared_ptr<const Model> model{};
				ASH_PROCESS_ERROR(database.load_model_by_path(path, model));
				result = instantiate_model(*model, parent);
			}
			else
			{
				ASH_PROCESS_ERROR(false);
			}
		}
		else
		// editor end
		{
			std::string extension = path.extension().string();
			std::transform(
				extension.begin(),
				extension.end(),
				extension.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			// editor begin 修改原因：支持 .mesh/.ashmesh 扩展名的 Mesh 资源
			if (extension == ".mesh" || extension == ".ashmesh")
			{
				std::shared_ptr<const Mesh> mesh{};
				ASH_PROCESS_ERROR(database.load_mesh_by_path(path, mesh));
				ASH_PROCESS_ERROR(mesh != nullptr);
				result = instantiate_mesh(mesh, path, parent);
			}
			else if (extension == ".ashasset")
			{
				std::shared_ptr<const AshAsset> asset{};
				ASH_PROCESS_ERROR(database.load_ashasset_by_path(path, asset));
				result = instantiate_ashasset(*asset, parent);
			}
			else
			{
				std::shared_ptr<const Model> model{};
				ASH_PROCESS_ERROR(database.load_model_by_path(path, model));
				result = instantiate_model(*model, parent);
			}
			// editor end
		}

		ASH_PROCESS_GUARD_RETURN_END(result, Entity{});
	}

	Entity instantiate_asset(
		Scene& scene,
		AssetDatabase& database,
		AssetId asset_id,
		const SceneInstantiationDesc& desc)
	{
		ASH_PROCESS_GUARD_RETURN(Entity, result, Entity{}, Entity{});
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(database.is_valid());
		ASH_PROCESS_ERROR(asset_id != 0);

		const AssetInfo* asset_info = database.find_asset_by_id(asset_id);
		ASH_PROCESS_ERROR(asset_info != nullptr);

		const std::string_view root_name_override(desc.root_name_override);

		// editor begin 修改原因：扩展 instantiate_asset 支持 AssetType::Mesh
		if (asset_info->type == AssetType::Mesh)
		{
			std::shared_ptr<const Mesh> mesh{};
			ASH_PROCESS_ERROR(database.load_mesh_by_id(asset_id, mesh));
			ASH_PROCESS_ERROR(mesh != nullptr);
			result = scene.instantiate_mesh(mesh, asset_info->relative_path, desc.parent, root_name_override);
		}
		else if (asset_info->type == AssetType::Prefab)
		{
			std::shared_ptr<const AshAsset> asset{};
			ASH_PROCESS_ERROR(database.load_ashasset_by_id(asset_id, asset));
			ASH_PROCESS_ERROR(asset != nullptr);
			result = scene.instantiate_ashasset(*asset, desc.parent, root_name_override);
		}
		else if (asset_info->type == AssetType::Model)
		{
			std::shared_ptr<const Model> model{};
			ASH_PROCESS_ERROR(database.load_model_by_id(asset_id, model));
			ASH_PROCESS_ERROR(model != nullptr);
			result = scene.instantiate_model(*model, desc.parent, root_name_override);
		}
		else
		{
			ASH_PROCESS_ERROR(false);
		}
		// editor end

		ASH_PROCESS_ERROR(result.is_valid());
		if (desc.use_world_transform)
		{
			TransformComponent world_transform{};
			world_transform.position = desc.world_position;
			world_transform.rotation_euler_degrees = desc.world_rotation_euler_degrees;
			world_transform.scale = desc.world_scale;

			if (desc.parent.is_valid())
			{
				glm::mat4 local_matrix = transform_component_to_matrix(world_transform);
				local_matrix = glm::inverse(scene.get_entity_world_transform(desc.parent.get_id())) * local_matrix;
				world_transform = matrix_to_transform_component(local_matrix);
			}

			ASH_PROCESS_ERROR(result.set_transform_component(world_transform));
		}

		ASH_PROCESS_GUARD_RETURN_END(result, Entity{});
	}

	bool Scene::save_to_file(const std::filesystem::path& path, std::string* out_error)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		if (!m_impl)
		{
			make_scene_error(out_error, "Scene is invalid.");
			ASH_PROCESS_ERROR(false);
		}

		std::error_code directory_error{};
		if (!path.parent_path().empty())
		{
			std::filesystem::create_directories(path.parent_path(), directory_error);
			if (directory_error)
			{
				make_scene_error(out_error, directory_error.message());
				ASH_PROCESS_ERROR(false);
			}
		}

		json root{};
		root["version"] = k_scene_file_version;
		root["name"] = m_impl->storage.name;
		root["next_entity_id"] = m_impl->storage.next_entity_id;
		root["scene_config"] = serialize_scene_render_config(m_impl->storage.render_config);
		root["entities"] = json::array();

		std::function<void(EntityId)> append_entity_json_recursive = [&](EntityId id)
		{
			const entt::entity handle = find_handle(m_impl, id);
			if (handle == entt::null)
			{
				return;
			}

			const NameComponent& name = m_impl->storage.registry.get<NameComponent>(handle);
			const TransformComponent& transform = m_impl->storage.registry.get<TransformComponent>(handle);
			const HierarchyComponent& hierarchy = m_impl->storage.registry.get<HierarchyComponent>(handle);

			json entity_json{};
			entity_json["id"] = id;
			entity_json["parent"] = hierarchy.parent;
			entity_json["name"] = name.value;
			entity_json["transform"] =
			{
				{ "position", to_json_vec3(transform.position) },
				{ "rotation_euler_degrees", to_json_vec3(transform.rotation_euler_degrees) },
				{ "scale", to_json_vec3(transform.scale) },
			};

			if (const CameraComponent* camera = m_impl->storage.registry.try_get<CameraComponent>(handle))
			{
				entity_json["camera"] =
				{
					{ "primary", camera->primary },
					{ "reverse_z", camera->reverse_z },
					{ "projection", static_cast<int32_t>(camera->projection) },
					{ "fov_y_degrees", camera->fov_y_degrees },
					{ "near_plane", camera->near_plane },
					{ "far_plane", camera->far_plane },
					{ "orthographic_height", camera->orthographic_height },
				};
			}

			if (const LightComponent* light = m_impl->storage.registry.try_get<LightComponent>(handle))
			{
				entity_json["light"] =
				{
					{ "type", static_cast<int32_t>(light->type) },
					{ "color", to_json_vec3(light->color) },
					{ "intensity", light->intensity },
					{ "range", light->range },
					{ "inner_cone_angle_degrees", light->inner_cone_angle_degrees },
					{ "outer_cone_angle_degrees", light->outer_cone_angle_degrees },
					{ "casts_shadow", light->casts_shadow },
					{ "sunlight", light->sunlight },
					{ "shadow_priority", light->shadow_priority },
					{ "shadow_distance", light->shadow_distance },
					{ "shadow_cascade_count", light->shadow_cascade_count },
					{ "near_shadow_distance", light->near_shadow_distance },
				};
			}

			if (const MeshComponent* mesh = m_impl->storage.registry.try_get<MeshComponent>(handle))
			{
				entity_json["mesh"] =
				{
					{ "asset_path", mesh->asset_path },
					{ "mesh_index", mesh->mesh_index },
					{ "visible", mesh->visible },
					{ "mobility", static_cast<uint32_t>(mesh->mobility) },
					{ "layer_mask", mesh->layer_mask },
				};
				if (!mesh->material_overrides.empty())
				{
					entity_json["mesh"]["material_overrides"] = serialize_material_overrides(mesh->material_overrides);
				}
			}

			if (const EnvironmentComponent* environment = m_impl->storage.registry.try_get<EnvironmentComponent>(handle))
			{
				entity_json["environment"] =
				{
					{ "active", environment->active },
					{ "ibl_asset_path", environment->ibl_asset_path },
					{ "source_texture_path", environment->source_texture_path },
					{ "intensity", environment->intensity },
					{ "lighting_intensity", environment->lighting_intensity },
					{ "background_intensity", environment->background_intensity },
					{ "rotation_degrees", environment->rotation_degrees },
					{ "visible_background", environment->visible_background },
					{ "affect_lighting", environment->affect_lighting },
				};
			}

			root["entities"].push_back(std::move(entity_json));
			for (EntityId child_id : hierarchy.children)
			{
				append_entity_json_recursive(child_id);
			}
		};

		for (EntityId root_id : m_impl->storage.root_order)
		{
			append_entity_json_recursive(root_id);
		}

		std::ofstream output(path);
		if (!output.is_open())
		{
			make_scene_error(out_error, "Failed to open scene output file.");
			ASH_PROCESS_ERROR(false);
		}

		try
		{
			output << root.dump(2);
		}
		catch (const std::exception& exception)
		{
			make_scene_error(out_error, exception.what());
			ASH_PROCESS_ERROR(false);
		}

		m_impl->storage.source_path = path;
		m_impl->storage.dirty = false;
		if (out_error)
		{
			out_error->clear();
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	const std::filesystem::path& Scene::get_source_path() const
	{
		static const std::filesystem::path k_empty_path{};
		return m_impl ? m_impl->storage.source_path : k_empty_path;
	}

	bool Scene::is_dirty() const
	{
		return m_impl ? m_impl->storage.dirty : false;
	}

	uint64_t Scene::get_change_version() const
	{
		return m_impl ? m_impl->storage.change_version : 0;
	}

	const SceneRenderConfig& Scene::get_render_config() const
	{
		static const SceneRenderConfig k_default_config = make_default_scene_render_config();
		return m_impl ? m_impl->storage.render_config : k_default_config;
	}

	bool Scene::set_render_config(const SceneRenderConfig& config)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(m_impl != nullptr);

		SceneRenderConfig sanitized_config{};
		sanitized_config.ambient_occlusion =
			sanitize_ambient_occlusion_config(config.ambient_occlusion, make_default_ambient_occlusion_config());
		sanitized_config.directional_shadows =
			sanitize_directional_shadow_config(config.directional_shadows, make_default_directional_shadow_config());
		sanitized_config.bloom =
			sanitize_bloom_config(config.bloom, make_default_bloom_config());
		sanitized_config.volumetric_lighting =
			sanitize_volumetric_lighting_config(config.volumetric_lighting, make_default_volumetric_lighting_config());

		if (!scene_render_config_equal(m_impl->storage.render_config, sanitized_config))
		{
			m_impl->storage.render_config = sanitized_config;
			mark_scene_storage_modified(m_impl->storage, SceneChangeKind::DirtyStateChanged);
			m_impl->storage.render_config_version = m_impl->storage.change_version;
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	uint64_t Scene::get_render_primitive_version() const
	{
		return m_impl ? m_impl->storage.render_primitive_version : 0;
	}

	uint64_t Scene::get_render_transform_version() const
	{
		return m_impl ? m_impl->storage.render_transform_version : 0;
	}

	uint64_t Scene::get_render_light_version() const
	{
		return m_impl ? m_impl->storage.render_light_version : 0;
	}

	uint64_t Scene::get_render_environment_version() const
	{
		return m_impl ? m_impl->storage.render_environment_version : 0;
	}

	uint64_t Scene::get_render_config_version() const
	{
		return m_impl ? m_impl->storage.render_config_version : 0;
	}

	void Scene::mark_clean()
	{
		if (m_impl)
		{
			mark_scene_storage_clean(m_impl->storage);
		}
	}

	// editor begin 修改原因：Scene Change Event 订阅接口实现
	uint32_t Scene::subscribe_change_events(SceneChangeCallback callback)
	{
		if (!m_impl || !callback)
		{
			return 0;
		}
		uint32_t id = m_impl->storage.next_subscription_id++;
		m_impl->storage.change_callbacks.emplace(id, std::move(callback));
		return id;
	}

	bool Scene::unsubscribe_change_events(uint32_t subscription_id)
	{
		if (!m_impl || subscription_id == 0)
		{
			return false;
		}
		return m_impl->storage.change_callbacks.erase(subscription_id) > 0;
	}

	void Scene::notify_change_event(const SceneChangeEvent& event)
	{
		if (!m_impl)
		{
			return;
		}
		for (const auto& [id, callback] : m_impl->storage.change_callbacks)
		{
			if (callback)
			{
				callback(event);
			}
		}
	}
	// editor end

	// editor begin 修改原因：通用 Add/Remove Component facade
	bool can_add_scene_component(const Entity& entity, SceneComponentType type)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Name:
		case SceneComponentType::Transform:
			return false;
		case SceneComponentType::Camera:
			return !entity.has_camera_component();
		case SceneComponentType::Light:
			return !entity.has_light_component();
		case SceneComponentType::Mesh:
			return !entity.has_mesh_component();
		case SceneComponentType::Environment:
			return !entity.has_environment_component();
		default:
			return false;
		}
	}

	bool can_remove_scene_component(const Entity& entity, SceneComponentType type)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Name:
		case SceneComponentType::Transform:
			return false;
		case SceneComponentType::Camera:
			return entity.has_camera_component();
		case SceneComponentType::Light:
			return entity.has_light_component();
		case SceneComponentType::Mesh:
			return entity.has_mesh_component();
		case SceneComponentType::Environment:
			return entity.has_environment_component();
		default:
			return false;
		}
	}

	bool add_scene_component(Entity& entity, SceneComponentType type)
	{
		if (!can_add_scene_component(entity, type))
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Camera:
			return entity.add_camera_component({});
		case SceneComponentType::Light:
			return entity.add_light_component({});
		case SceneComponentType::Mesh:
			return entity.add_mesh_component({});
		case SceneComponentType::Environment:
			return entity.add_environment_component({});
		default:
			return false;
		}
	}

	bool remove_scene_component(Entity& entity, SceneComponentType type)
	{
		if (!can_remove_scene_component(entity, type))
		{
			return false;
		}

		switch (type)
		{
		case SceneComponentType::Camera:
			return entity.remove_camera_component();
		case SceneComponentType::Light:
			return entity.remove_light_component();
		case SceneComponentType::Mesh:
			return entity.remove_mesh_component();
		case SceneComponentType::Environment:
			return entity.remove_environment_component();
		default:
			return false;
		}
	}
	// editor end

	const SceneComponentDesc* get_scene_component_descriptor(SceneComponentType type)
	{
		for (const SceneComponentDesc& desc : k_scene_component_descs)
		{
			if (desc.type == type)
			{
				return &desc;
			}
		}
		return nullptr;
	}

	const SceneComponentDesc* get_scene_component_descriptors(uint32_t* out_count)
	{
		if (out_count)
		{
			*out_count = static_cast<uint32_t>(std::size(k_scene_component_descs));
		}
		return k_scene_component_descs;
	}

	const SceneEnumDesc* get_scene_enum_descriptor(const char* name)
	{
		if (!name)
		{
			return nullptr;
		}

		for (const SceneEnumDesc& desc : k_scene_enum_descs)
		{
			if (desc.name && std::string_view(desc.name) == std::string_view(name))
			{
				return &desc;
			}
		}
		return nullptr;
	}
}
