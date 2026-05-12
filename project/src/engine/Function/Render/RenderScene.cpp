#include "Function/Render/RenderScene.h"

#include "Base/hprofiler.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/Visibility.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>

namespace AshEngine
{
	namespace
	{
		static auto normalize_or_fallback(const glm::vec3& value, const glm::vec3& fallback) -> glm::vec3
		{
			const float length = glm::length(value);
			return length > 0.0001f ? value / length : fallback;
		}

		static auto make_visible_light_data(const SceneLightExtractionDesc& desc, VisibleLightData& out_light) -> bool
		{
			const LightComponent& light = desc.light;
			if (light.intensity <= 0.0f)
			{
				return false;
			}
			if ((light.type == LightType::Point || light.type == LightType::Spot) && light.range <= 0.0f)
			{
				return false;
			}

			out_light = {};
			out_light.entity_id = desc.entity_id;
			out_light.type = light.type;
			out_light.position_ws = glm::vec3(desc.world_transform[3]);
			out_light.direction_ws = normalize_or_fallback(
				glm::vec3(desc.world_transform * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)),
				glm::vec3(0.0f, 0.0f, 1.0f));
			out_light.color = glm::max(light.color, glm::vec3(0.0f));
			out_light.intensity = light.intensity;
			out_light.range = std::max(light.range, 0.0f);

			const float outer_degrees = std::clamp(light.outer_cone_angle_degrees, 0.1f, 89.0f);
			const float inner_degrees = std::clamp(light.inner_cone_angle_degrees, 0.0f, outer_degrees);
			out_light.inner_cone_cos = std::cos(glm::radians(inner_degrees));
			out_light.outer_cone_cos = std::cos(glm::radians(outer_degrees));
			return true;
		}
	}

	RenderScene::RenderScene(const RenderScene& other)
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = other.m_static_mesh_primitives;
		m_lights = other.m_lights;
	}

	RenderScene::RenderScene(RenderScene&& other) noexcept
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = std::move(other.m_static_mesh_primitives);
		m_lights = std::move(other.m_lights);
		other.m_next_primitive_id = 1;
	}

	RenderScene& RenderScene::operator=(const RenderScene& other)
	{
		if (this == &other)
		{
			return *this;
		}
		std::scoped_lock<std::mutex, std::mutex> lock(m_mutex, other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = other.m_static_mesh_primitives;
		m_lights = other.m_lights;
		return *this;
	}

	RenderScene& RenderScene::operator=(RenderScene&& other) noexcept
	{
		if (this == &other)
		{
			return *this;
		}
		std::scoped_lock<std::mutex, std::mutex> lock(m_mutex, other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = std::move(other.m_static_mesh_primitives);
		m_lights = std::move(other.m_lights);
		other.m_next_primitive_id = 1;
		return *this;
	}

	bool RenderScene::rebuild_from_scene(Scene& scene, RenderAssetManager& render_asset_manager)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(render_asset_manager.get_asset_database() != nullptr);

		AssetDatabase& asset_database = *render_asset_manager.get_asset_database();

		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> rebuilt_primitives{};
		uint64_t next_primitive_id = 1;
		for (const SceneMeshExtractionDesc& mesh_desc : scene.extract_visible_mesh_entities())
		{
			std::shared_ptr<StaticMeshRenderAsset> render_asset = render_asset_manager.request_static_mesh_asset(mesh_desc.asset_path, mesh_desc.mesh_index);
			ASH_PROCESS_ERROR(render_asset);
			ASH_PROCESS_ERROR(render_asset->is_cpu_ready());

			SceneMeshBounds local_bounds{};
			MeshComponent mesh_component{};
			mesh_component.asset_path = mesh_desc.asset_path;
			mesh_component.mesh_index = mesh_desc.mesh_index;
			mesh_component.material_overrides = mesh_desc.material_overrides;
			mesh_component.visible = mesh_desc.visible;
			mesh_component.mobility = mesh_desc.mobility;
			mesh_component.layer_mask = mesh_desc.layer_mask;
			ASH_PROCESS_ERROR(scene.try_get_mesh_local_bounds(asset_database, mesh_component, local_bounds));

			std::vector<ResolvedStaticMeshSection> resolved_sections{};
			ASH_PROCESS_ERROR(
				render_asset_manager.resolve_static_mesh_primitive_sections(
					*render_asset,
					mesh_desc.material_overrides,
					resolved_sections));

			auto primitive = std::make_shared<StaticMeshPrimitiveProxy>();
			primitive->initialize(next_primitive_id++, mesh_desc, render_asset, local_bounds, std::move(resolved_sections));
			rebuilt_primitives.push_back(std::move(primitive));
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_static_mesh_primitives = std::move(rebuilt_primitives);
			m_next_primitive_id = next_primitive_id;
		}
		ASH_PROCESS_ERROR(rebuild_lights_from_scene(scene));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::rebuild_lights_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_lights_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		std::vector<VisibleLightData> rebuilt_lights{};
		for (const SceneLightExtractionDesc& light_desc : scene.extract_light_entities())
		{
			VisibleLightData visible_light{};
			if (make_visible_light_data(light_desc, visible_light))
			{
				rebuilt_lights.push_back(visible_light);
			}
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_lights = std::move(rebuilt_lights);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_render_frame(
		uint64_t frame_index,
		const SceneView& view,
		VisibleRenderFrame& out_frame) const
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::build_visible_render_frame", AshEngine::Profile::Color::Visibility);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_frame = {};
		ASH_PROCESS_ERROR(view.is_valid);

		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> primitives_snapshot;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			primitives_snapshot = m_static_mesh_primitives;
		}

		VisibilityResult visibility{};
		ASH_PROCESS_ERROR(run_visibility_query({ &primitives_snapshot, &view }, visibility));

		out_frame.frame_index = frame_index;
		out_frame.view = view.view;
		out_frame.projection = view.projection;
		out_frame.view_projection = view.view_projection;
		out_frame.camera_position = view.camera_position;
		ASH_PROCESS_ERROR(build_visible_light_frame(out_frame));

		std::unordered_map<uint64_t, const StaticMeshPrimitiveProxy*> primitive_index;
		primitive_index.reserve(primitives_snapshot.size());
		for (const std::shared_ptr<StaticMeshPrimitiveProxy>& primitive : primitives_snapshot)
		{
			if (primitive)
			{
				primitive_index.emplace(primitive->get_primitive_id(), primitive.get());
			}
		}

		out_frame.static_mesh_draws.reserve(visibility.visible_primitives.primitive_ids.size());
		for (uint64_t primitive_id : visibility.visible_primitives.primitive_ids)
		{
			const auto found = primitive_index.find(primitive_id);
			if (found == primitive_index.end() || found->second == nullptr)
			{
				continue;
			}

			const StaticMeshPrimitiveProxy& primitive = *found->second;
			VisibleStaticMeshDraw draw{};
			draw.primitive_id = primitive.get_primitive_id();
			draw.entity_id = primitive.get_entity_id();
			draw.world_transform = primitive.get_world_transform();
			draw.render_asset = primitive.get_render_asset();
			draw.sections = primitive.get_sections();
			out_frame.static_mesh_draws.push_back(std::move(draw));
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_light_frame(VisibleRenderFrame& out_frame) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		out_frame.lights = m_lights;
		return true;
	}

	std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> RenderScene::get_static_mesh_primitives_snapshot() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_static_mesh_primitives;
	}
}
