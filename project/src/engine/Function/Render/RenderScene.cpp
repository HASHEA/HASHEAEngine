#include "Function/Render/RenderScene.h"

#include "Base/hprofiler.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/Visibility.h"
#include <algorithm>
#include <cmath>
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
			out_light.casts_shadow = light.casts_shadow;
			out_light.sunlight = light.type == LightType::Directional && light.sunlight;
			out_light.shadow_priority = light.shadow_priority;
			out_light.shadow_distance = light.shadow_distance;
			out_light.shadow_cascade_count = light.shadow_cascade_count;
			out_light.near_shadow_distance = light.near_shadow_distance;
			return true;
		}

		static auto intersects_frustum(const PrimitiveBounds& bounds, const SceneView& view) -> bool
		{
			if (!bounds.is_valid)
			{
				return false;
			}

			for (const SceneFrustumPlane& plane : view.frustum_planes)
			{
				const glm::vec3 positive_vertex =
				{
					plane.normal.x >= 0.0f ? bounds.world_max.x : bounds.world_min.x,
					plane.normal.y >= 0.0f ? bounds.world_max.y : bounds.world_min.y,
					plane.normal.z >= 0.0f ? bounds.world_max.z : bounds.world_min.z
				};
				if (glm::dot(plane.normal, positive_vertex) + plane.distance < 0.0f)
				{
					return false;
				}
			}
			return true;
		}
	}

	RenderScene::RenderScene(const RenderScene& other)
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = other.m_static_mesh_primitives;
		m_terrain_proxies = other.m_terrain_proxies;
		m_lights = other.m_lights;
		m_environment = other.m_environment;
		m_particle_emitters = other.m_particle_emitters;
		m_render_config = other.m_render_config;
	}

	RenderScene::RenderScene(RenderScene&& other) noexcept
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = std::move(other.m_static_mesh_primitives);
		m_terrain_proxies = std::move(other.m_terrain_proxies);
		m_lights = std::move(other.m_lights);
		m_environment = std::move(other.m_environment);
		m_particle_emitters = std::move(other.m_particle_emitters);
		m_render_config = std::move(other.m_render_config);
		other.m_next_primitive_id = 1;
		other.m_render_config = make_default_scene_render_config();
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
		m_terrain_proxies = other.m_terrain_proxies;
		m_lights = other.m_lights;
		m_environment = other.m_environment;
		m_particle_emitters = other.m_particle_emitters;
		m_render_config = other.m_render_config;
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
		m_terrain_proxies = std::move(other.m_terrain_proxies);
		m_lights = std::move(other.m_lights);
		m_environment = std::move(other.m_environment);
		m_particle_emitters = std::move(other.m_particle_emitters);
		m_render_config = std::move(other.m_render_config);
		other.m_next_primitive_id = 1;
		other.m_render_config = make_default_scene_render_config();
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
		ASH_PROCESS_ERROR(rebuild_terrains_from_scene(scene, render_asset_manager));
		ASH_PROCESS_ERROR(rebuild_lights_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_environment_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_particles_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_render_config_from_scene(scene));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::rebuild_terrains_from_scene(
		Scene& scene,
		RenderAssetManager& render_asset_manager)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_terrains_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());
		AssetDatabase* asset_database = render_asset_manager.get_asset_database();
		ASH_PROCESS_ERROR(asset_database != nullptr);

		std::vector<std::shared_ptr<RenderTerrainProxy>> rebuilt_proxies{};
		const std::vector<SceneTerrainExtractionDesc> terrain_entities =
			scene.extract_terrain_entities();
		rebuilt_proxies.reserve(terrain_entities.size());
		for (const SceneTerrainExtractionDesc& terrain_desc : terrain_entities)
		{
			std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
			ASH_PROCESS_ERROR(asset_database->load_terrain_by_path(
				terrain_desc.terrain.asset_path,
				snapshot));
			ASH_PROCESS_ERROR(snapshot && !snapshot->failed);

			std::shared_ptr<TerrainRenderAsset> render_asset =
				render_asset_manager.request_terrain_asset(
					terrain_desc.terrain.asset_path,
					snapshot);
			ASH_PROCESS_ERROR(render_asset != nullptr);

			auto proxy = std::make_shared<RenderTerrainProxy>();
			ASH_PROCESS_ERROR(proxy->initialize(
				terrain_desc.entity_id,
				snapshot,
				terrain_desc.world_transform,
				terrain_desc.terrain.visible,
				terrain_desc.terrain.casts_shadow,
				terrain_desc.terrain.receives_shadow,
				std::move(render_asset)));
			rebuilt_proxies.push_back(std::move(proxy));
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_terrain_proxies = std::move(rebuilt_proxies);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::update_terrain_transforms_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::update_terrain_transforms_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		std::scoped_lock<std::mutex> lock(m_mutex);
		std::vector<std::shared_ptr<RenderTerrainProxy>> updated_proxies{};
		updated_proxies.reserve(m_terrain_proxies.size());
		for (const std::shared_ptr<RenderTerrainProxy>& proxy : m_terrain_proxies)
		{
			ASH_PROCESS_ERROR(proxy != nullptr);
			const Entity entity = scene.find_entity(proxy->get_entity_id());
			ASH_PROCESS_ERROR(entity.is_valid() && entity.has_terrain_component());

			auto updated_proxy = std::make_shared<RenderTerrainProxy>(*proxy);
			ASH_PROCESS_ERROR(updated_proxy->update_world_transform(
				scene.get_entity_world_transform(proxy->get_entity_id())));
			updated_proxies.push_back(std::move(updated_proxy));
		}
		m_terrain_proxies = std::move(updated_proxies);
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::update_transforms_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::update_transforms_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		std::scoped_lock<std::mutex> lock(m_mutex);
		for (const std::shared_ptr<StaticMeshPrimitiveProxy>& primitive : m_static_mesh_primitives)
		{
			if (!primitive)
			{
				continue;
			}

			const Entity entity = scene.find_entity(primitive->get_entity_id());
			if (!entity.is_valid() || !entity.has_mesh_component())
			{
				continue;
			}

			primitive->update_world_transform(scene.get_entity_world_transform(primitive->get_entity_id()));
		}
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

	bool RenderScene::rebuild_environment_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_environment_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		SceneEnvironmentExtractionDesc extracted{};
		if (!scene.extract_active_environment(extracted))
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_environment.reset();
			return true;
		}

		VisibleEnvironmentData environment{};
		environment.entity_id = extracted.entity_id;
		environment.ibl_asset_path = extracted.ibl_asset_path;
		environment.source_texture_path = extracted.source_texture_path;
		environment.intensity = extracted.intensity;
		environment.lighting_intensity = extracted.lighting_intensity;
		environment.background_intensity = extracted.background_intensity;
		environment.rotation_degrees = extracted.rotation_degrees;
		environment.visible_background = extracted.visible_background;
		environment.affect_lighting = extracted.affect_lighting;

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_environment = std::move(environment);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::rebuild_particles_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_particles_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		std::vector<VisibleParticleEmitter> rebuilt_emitters{};
		for (const SceneParticleExtractionDesc& particle_desc : scene.extract_particle_entities())
		{
			VisibleParticleEmitter emitter{};
			emitter.entity_id = particle_desc.entity_id;
			emitter.particle = particle_desc.particle;
			emitter.particle.max_particles = std::min(emitter.particle.max_particles, k_max_particles_per_emitter);
			emitter.world_transform = particle_desc.world_transform;
			rebuilt_emitters.push_back(std::move(emitter));
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_particle_emitters = std::move(rebuilt_emitters);
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::rebuild_render_config_from_scene(const Scene& scene)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_render_config_from_scene", AshEngine::Profile::Color::Scene);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(scene.is_valid());

		std::scoped_lock<std::mutex> lock(m_mutex);
		m_render_config = scene.get_render_config();
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_render_frame(
		uint64_t frame_index,
		const SceneView& view,
		VisibleRenderFrame& out_frame,
		uint64_t static_scene_revision,
		uint64_t transform_scene_revision,
		uint64_t light_scene_revision) const
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::build_visible_render_frame", AshEngine::Profile::Color::Visibility);
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_frame = {};
		ASH_PROCESS_ERROR(view.is_valid);

		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> primitives_snapshot;
		std::vector<std::shared_ptr<RenderTerrainProxy>> terrain_snapshot;
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			primitives_snapshot = m_static_mesh_primitives;
			terrain_snapshot = m_terrain_proxies;
		}

		VisibilityResult visibility{};
		ASH_PROCESS_ERROR(run_visibility_query({ &primitives_snapshot, &view }, visibility));

		out_frame.frame_index = frame_index;
		out_frame.view = view.view;
		out_frame.projection = view.projection;
		out_frame.view_projection = view.view_projection;
		out_frame.camera_position = view.camera_position;
		out_frame.reverse_z = view.reverse_z;
		out_frame.static_scene_revision = static_scene_revision;
		out_frame.transform_scene_revision = transform_scene_revision;
		out_frame.light_scene_revision = light_scene_revision;
		ASH_PROCESS_ERROR(build_visible_light_frame(out_frame));

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			out_frame.environment = m_environment;
			out_frame.particle_emitters = m_particle_emitters;
		}

		out_frame.static_mesh_draws.reserve(visibility.visible_primitives.primitives.size());
		for (const StaticMeshPrimitiveProxy* primitive_ptr : visibility.visible_primitives.primitives)
		{
			if (!primitive_ptr)
			{
				continue;
			}

			const StaticMeshPrimitiveProxy& primitive = *primitive_ptr;
			VisibleStaticMeshDraw draw{};
			draw.primitive_id = primitive.get_primitive_id();
			draw.entity_id = primitive.get_entity_id();
			draw.world_transform = primitive.get_world_transform();
			draw.render_asset = primitive.get_render_asset();
			draw.sections = primitive.get_sections();
			draw.bounds = primitive.get_bounds();
			draw.mobility = primitive.get_mobility();
			out_frame.static_mesh_draws.push_back(std::move(draw));
		}

		out_frame.shadow_caster_static_mesh_draws.reserve(primitives_snapshot.size());
		for (const std::shared_ptr<StaticMeshPrimitiveProxy>& primitive_ptr : primitives_snapshot)
		{
			if (!primitive_ptr || !primitive_ptr->is_visible())
			{
				continue;
			}

			const StaticMeshPrimitiveProxy& primitive = *primitive_ptr;
			VisibleStaticMeshDraw draw{};
			draw.primitive_id = primitive.get_primitive_id();
			draw.entity_id = primitive.get_entity_id();
			draw.world_transform = primitive.get_world_transform();
			draw.render_asset = primitive.get_render_asset();
			draw.sections = primitive.get_sections();
			draw.bounds = primitive.get_bounds();
			draw.mobility = primitive.get_mobility();
			out_frame.shadow_caster_static_mesh_draws.push_back(std::move(draw));
		}

		out_frame.terrains.reserve(terrain_snapshot.size());
		for (const std::shared_ptr<RenderTerrainProxy>& terrain : terrain_snapshot)
		{
			if (!terrain || !terrain->is_visible() ||
				!intersects_frustum(terrain->get_bounds(), view))
			{
				continue;
			}
			out_frame.terrains.push_back(terrain->make_visible_frame());
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_light_frame(VisibleRenderFrame& out_frame) const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		out_frame.lights = m_lights;
		out_frame.render_config = m_render_config;
		return true;
	}

	std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> RenderScene::get_static_mesh_primitives_snapshot() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_static_mesh_primitives;
	}
}
