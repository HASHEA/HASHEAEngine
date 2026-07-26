#include "Function/Render/RenderScene.h"

#include "Base/hprofiler.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/Visibility.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <unordered_set>

namespace AshEngine
{
	namespace
	{
		constexpr uint64_t k_shadow_revision_fnv_offset = 14695981039346656037ull;
		constexpr uint64_t k_shadow_revision_fnv_prime = 1099511628211ull;

		static void hash_shadow_revision_u64(uint64_t& hash, uint64_t value)
		{
			for (uint32_t byte = 0u; byte < 8u; ++byte)
			{
				hash ^= static_cast<uint8_t>(value >> (byte * 8u));
				hash *= k_shadow_revision_fnv_prime;
			}
		}

		static void hash_shadow_revision_float(uint64_t& hash, float value)
		{
			uint32_t bits = 0u;
			static_assert(sizeof(bits) == sizeof(value));
			std::memcpy(&bits, &value, sizeof(bits));
			hash_shadow_revision_u64(hash, bits);
		}

		static void hash_shadow_revision_pointer(
			uint64_t& hash,
			const void* value)
		{
			hash_shadow_revision_u64(
				hash,
				static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value)));
		}

		static void hash_shadow_revision_string(
			uint64_t& hash,
			const std::string& value)
		{
			hash_shadow_revision_u64(hash, value.size());
			for (const unsigned char byte : value)
			{
				hash ^= byte;
				hash *= k_shadow_revision_fnv_prime;
			}
		}

		static void hash_shadow_revision_matrix(
			uint64_t& hash,
			const glm::mat4& matrix)
		{
			for (glm::length_t column = 0; column < 4; ++column)
			{
				for (glm::length_t row = 0; row < 4; ++row)
				{
					hash_shadow_revision_float(hash, matrix[column][row]);
				}
			}
		}

		static auto is_static_shadow_caster(
			const VisibleStaticMeshDraw& draw) -> bool
		{
			return draw.mobility == SceneMobility::Static ||
				draw.mobility == SceneMobility::Stationary;
		}

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

	bool terrain_scene_resolve_transition_changed(
		const TerrainSceneResolveResult& previous,
		const TerrainSceneResolveResult& current)
	{
		return previous.status != current.status ||
			previous.asset_path != current.asset_path ||
			previous.diagnostic != current.diagnostic ||
			previous.content_generation != current.content_generation ||
			previous.drawable_published_view !=
				current.drawable_published_view;
	}

	TerrainReadinessStage evaluate_terrain_scene_resolve_readiness(
		TerrainSceneResolveStatus status)
	{
		switch (status)
		{
		case TerrainSceneResolveStatus::Failed:
			return TerrainReadinessStage::Failed;
		case TerrainSceneResolveStatus::Pending:
			return TerrainReadinessStage::Pending;
		case TerrainSceneResolveStatus::Ready:
		default:
			return TerrainReadinessStage::Ready;
		}
	}

	uint64_t compute_terrain_temporal_signature(
		const VisibleRenderFrame& frame)
	{
		uint64_t hash = k_shadow_revision_fnv_offset;
		hash_shadow_revision_u64(hash, frame.terrains.size());
		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			hash_shadow_revision_u64(hash, terrain.entity_id);
			if (terrain.published_view)
			{
				hash_shadow_revision_u64(
					hash, terrain.published_view->asset_id);
				hash_shadow_revision_u64(
					hash, terrain.published_view->content_generation);
				hash_shadow_revision_u64(
					hash, terrain.published_view->residency_revision);
				hash_shadow_revision_u64(
					hash, terrain.published_view->publication_epoch);
			}
			else if (terrain.asset_snapshot)
			{
				hash_shadow_revision_u64(
					hash, terrain.asset_snapshot->asset_id);
				hash_shadow_revision_u64(
					hash, terrain.asset_snapshot->content_generation);
				hash_shadow_revision_u64(
					hash, terrain.asset_snapshot->residency_revision);
			}
			else
			{
				hash_shadow_revision_u64(hash, 0u);
				hash_shadow_revision_u64(hash, 0u);
				hash_shadow_revision_u64(hash, 0u);
			}
		}
		return hash == 0u ? 1u : hash;
	}

	bool terrain_frame_asset_epoch_is_current(
		const VisibleRenderFrame& frame,
		uint64_t current_asset_epoch)
	{
		return frame.render_asset_epoch == current_asset_epoch;
	}

	uint64_t compute_static_shadow_caster_revision(
		const VisibleRenderFrame& frame)
	{
		uint64_t hash = k_shadow_revision_fnv_offset;
		hash_shadow_revision_u64(hash, frame.scene_runtime_id);
		hash_shadow_revision_u64(hash, frame.scene_content_epoch);

		const size_t static_mesh_caster_count = static_cast<size_t>(
			std::count_if(
				frame.shadow_caster_static_mesh_draws.begin(),
				frame.shadow_caster_static_mesh_draws.end(),
				is_static_shadow_caster));
		hash_shadow_revision_u64(hash, static_mesh_caster_count);
		for (const VisibleStaticMeshDraw& draw :
			frame.shadow_caster_static_mesh_draws)
		{
			if (!is_static_shadow_caster(draw))
			{
				continue;
			}

			hash_shadow_revision_u64(hash, draw.primitive_id);
			hash_shadow_revision_u64(hash, draw.entity_id);
			hash_shadow_revision_u64(
				hash, static_cast<uint64_t>(draw.mobility));
			hash_shadow_revision_matrix(hash, draw.world_transform);

			const StaticMeshRenderAsset* render_asset = draw.render_asset.get();
			hash_shadow_revision_pointer(hash, render_asset);
			if (render_asset)
			{
				std::scoped_lock<std::mutex> asset_lock(render_asset->mutex);
				hash_shadow_revision_string(hash, render_asset->asset_path);
				hash_shadow_revision_u64(hash, render_asset->mesh_index);
				hash_shadow_revision_u64(
					hash, static_cast<uint64_t>(render_asset->state));
				hash_shadow_revision_u64(hash, render_asset->load_generation);

				const StaticMeshRenderResource* resource =
					render_asset->resource.get();
				hash_shadow_revision_pointer(hash, resource);
				if (resource)
				{
					hash_shadow_revision_pointer(
						hash, resource->vertex_buffer.get());
					hash_shadow_revision_pointer(
						hash, resource->index_buffer.get());
					hash_shadow_revision_pointer(
						hash, resource->vertex_decl.get());
					hash_shadow_revision_u64(hash, resource->vertex_count);
					hash_shadow_revision_u64(hash, resource->index_count);
					hash_shadow_revision_u64(
						hash, static_cast<uint64_t>(resource->index_format));
				}
			}

			hash_shadow_revision_u64(hash, draw.sections.size());
			for (const ResolvedStaticMeshSection& section : draw.sections)
			{
				hash_shadow_revision_u64(hash, section.first_index);
				hash_shadow_revision_u64(hash, section.index_count);
				hash_shadow_revision_u64(hash, section.material_slot);
				hash_shadow_revision_u64(
					hash, static_cast<uint64_t>(section.topology));
				hash_shadow_revision_pointer(hash, section.material.get());
				hash_shadow_revision_pointer(
					hash, section.material_proxy.get());
				hash_shadow_revision_u64(
					hash, section.depth_only_publication_identity);
			}
		}

		const size_t terrain_caster_count = static_cast<size_t>(
			std::count_if(
				frame.terrains.begin(),
				frame.terrains.end(),
				[](const VisibleTerrainFrame& terrain)
				{
					return terrain.casts_shadow;
				}));
		hash_shadow_revision_u64(hash, terrain_caster_count);

		for (const VisibleTerrainFrame& terrain : frame.terrains)
		{
			if (!terrain.casts_shadow)
			{
				continue;
			}
			hash_shadow_revision_u64(hash, terrain.entity_id);

			const TerrainAssetSnapshot* snapshot = terrain.asset_snapshot.get();
			hash_shadow_revision_pointer(hash, snapshot);
			if (snapshot)
			{
				hash_shadow_revision_u64(hash, snapshot->asset_id);
				hash_shadow_revision_u64(hash, snapshot->content_generation);
				hash_shadow_revision_u64(hash, snapshot->residency_revision);
			}

			const TerrainRenderAsset* render_asset = terrain.render_asset.get();
			hash_shadow_revision_pointer(hash, render_asset);
			if (terrain.published_view)
			{
				hash_shadow_revision_u64(
					hash, terrain.published_view->asset_id);
				hash_shadow_revision_u64(
					hash, terrain.published_view->content_generation);
				hash_shadow_revision_u64(
					hash, terrain.published_view->residency_revision);
				hash_shadow_revision_u64(
					hash, terrain.published_view->publication_epoch);
			}

			hash_shadow_revision_matrix(hash, terrain.world_transform);
		}

		return hash == 0u ? 1u : hash;
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
		m_pending_terrain_loads = other.m_pending_terrain_loads;
		m_last_terrain_resolve_result = other.m_last_terrain_resolve_result;
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
		m_pending_terrain_loads = std::move(other.m_pending_terrain_loads);
		m_last_terrain_resolve_result =
			std::move(other.m_last_terrain_resolve_result);
		other.m_next_primitive_id = 1;
		other.m_render_config = make_default_scene_render_config();
		other.m_last_terrain_resolve_result = {};
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
		m_pending_terrain_loads = other.m_pending_terrain_loads;
		m_last_terrain_resolve_result = other.m_last_terrain_resolve_result;
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
		m_pending_terrain_loads = std::move(other.m_pending_terrain_loads);
		m_last_terrain_resolve_result =
			std::move(other.m_last_terrain_resolve_result);
		other.m_next_primitive_id = 1;
		other.m_render_config = make_default_scene_render_config();
		other.m_last_terrain_resolve_result = {};
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
		(void)rebuild_terrains_from_scene(scene, render_asset_manager);
		ASH_PROCESS_ERROR(rebuild_lights_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_environment_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_particles_from_scene(scene));
		ASH_PROCESS_ERROR(rebuild_render_config_from_scene(scene));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	TerrainSceneResolveResult RenderScene::rebuild_terrains_from_scene(
		Scene& scene,
		RenderAssetManager& render_asset_manager)
	{
		ASH_PROFILE_SCOPE_NC("RenderScene::rebuild_terrains_from_scene", AshEngine::Profile::Color::Scene);
		auto PublishResult = [this](TerrainSceneResolveResult result)
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_last_terrain_resolve_result = result;
			return result;
		};
		auto MakeFailure = [this, &PublishResult](
			std::string asset_path,
			std::string diagnostic,
			uint64_t content_generation = 0u,
			bool retain_published_proxies = false)
		{
			const std::string normalized_asset_path =
				std::filesystem::path(asset_path)
					.lexically_normal()
					.generic_string();
			if (!retain_published_proxies && !normalized_asset_path.empty())
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				retain_published_proxies = std::any_of(
					m_terrain_proxies.begin(),
					m_terrain_proxies.end(),
					[&normalized_asset_path](
						const std::shared_ptr<RenderTerrainProxy>& proxy)
					{
						return proxy &&
							std::filesystem::path(proxy->get_asset_path())
								.lexically_normal()
								.generic_string() == normalized_asset_path &&
							proxy->get_render_asset() &&
							proxy->get_render_asset()->published_view();
					});
			}
			TerrainSceneResolveResult result{};
			result.status = TerrainSceneResolveStatus::Failed;
			result.asset_path = normalized_asset_path;
			result.diagnostic = std::move(diagnostic);
			result.content_generation = content_generation;
			result.drawable_published_view = retain_published_proxies;
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				if (retain_published_proxies)
				{
					m_pending_terrain_loads.erase(normalized_asset_path);
				}
				else
				{
					m_terrain_proxies.clear();
					m_pending_terrain_loads.clear();
				}
			}
			return PublishResult(std::move(result));
		};

		if (!scene.is_valid())
		{
			return MakeFailure({}, "Scene is invalid.");
		}
		AssetDatabase* asset_database = render_asset_manager.get_asset_database();
		if (!asset_database)
		{
			return MakeFailure({}, "RenderAssetManager has no AssetDatabase.");
		}

		const std::vector<SceneTerrainExtractionDesc> terrain_entities =
			scene.extract_terrain_entities();
		if (terrain_entities.empty())
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_terrain_proxies.clear();
			m_pending_terrain_loads.clear();
			m_last_terrain_resolve_result = {};
			return m_last_terrain_resolve_result;
		}

		struct ResolvedTerrain
		{
			SceneTerrainExtractionDesc desc{};
			std::string canonical_path{};
			AssetId asset_id = 0u;
			std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
		};
		std::vector<ResolvedTerrain> resolved_terrains{};
		resolved_terrains.reserve(terrain_entities.size());
		std::vector<std::string> active_paths{};
		active_paths.reserve(terrain_entities.size());
		std::unordered_map<EntityId, std::string> expected_asset_paths{};
		expected_asset_paths.reserve(terrain_entities.size());
		TerrainSceneResolveResult pending_result{};
		pending_result.status = TerrainSceneResolveStatus::Ready;

		for (const SceneTerrainExtractionDesc& terrain_desc : terrain_entities)
		{
			const AssetInfo* asset_info = asset_database->find_asset_by_path(
				terrain_desc.terrain.asset_path);
			if (!asset_info || asset_info->is_directory ||
				asset_info->type != AssetType::Terrain)
			{
				return MakeFailure(
					std::filesystem::path(terrain_desc.terrain.asset_path)
						.lexically_normal().generic_string(),
					"Terrain asset path was not found as a Terrain asset.");
			}

			const AssetInfo copied_info = *asset_info;
			const std::string canonical_path =
				copied_info.relative_path.lexically_normal().generic_string();
			active_paths.push_back(canonical_path);
			expected_asset_paths[terrain_desc.entity_id] = canonical_path;
			TerrainSceneLoadRequest request{};
			{
				std::scoped_lock<std::mutex> lock(m_mutex);
				const auto found = m_pending_terrain_loads.find(canonical_path);
				if (found != m_pending_terrain_loads.end() &&
					found->second.asset_id == copied_info.id)
				{
					request = found->second;
				}
			}
			if (!request.future.valid())
			{
				request.asset_id = copied_info.id;
				request.future = asset_database->load_terrain_by_path_async(
					copied_info.relative_path);
				std::scoped_lock<std::mutex> lock(m_mutex);
				m_pending_terrain_loads[canonical_path] = request;
			}
			if (!request.future.valid())
			{
				return MakeFailure(
					canonical_path,
					"Terrain asynchronous load request is invalid.");
			}

			if (request.future.wait_for(std::chrono::seconds(0)) !=
				std::future_status::ready)
			{
				if (pending_result.status != TerrainSceneResolveStatus::Pending)
				{
					pending_result.status = TerrainSceneResolveStatus::Pending;
					pending_result.asset_path = canonical_path;
				}
				continue;
			}

			std::shared_ptr<const TerrainAssetSnapshot> snapshot{};
			try
			{
				snapshot = request.future.get();
			}
			catch (const std::exception& exception)
			{
				return MakeFailure(canonical_path, exception.what());
			}
			catch (...)
			{
				return MakeFailure(
					canonical_path,
					"Terrain asynchronous load raised an unknown exception.");
			}

			if (!snapshot || snapshot->failed)
			{
				std::string diagnostic = snapshot ? snapshot->failure_detail :
					asset_database->get_asset_last_error(copied_info.id);
				if (diagnostic.empty())
				{
					diagnostic = "Terrain asynchronous load returned no valid snapshot.";
				}
				return MakeFailure(
					canonical_path,
					std::move(diagnostic),
					snapshot ? snapshot->content_generation : 0u);
			}

			resolved_terrains.push_back({
				terrain_desc,
				canonical_path,
				copied_info.id,
				std::move(snapshot)
			});
		}

		if (pending_result.status == TerrainSceneResolveStatus::Pending)
		{
			const std::unordered_set<std::string> active_path_set(
				active_paths.begin(), active_paths.end());
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_terrain_proxies.erase(
				std::remove_if(
					m_terrain_proxies.begin(),
					m_terrain_proxies.end(),
					[&expected_asset_paths](
						const std::shared_ptr<RenderTerrainProxy>& proxy)
					{
						if (!proxy)
						{
							return true;
						}
						const auto expected = expected_asset_paths.find(
							proxy->get_entity_id());
						const auto snapshot = proxy->get_snapshot();
						return expected == expected_asset_paths.end() ||
							!snapshot || snapshot->failed ||
							std::filesystem::path(proxy->get_asset_path())
								.lexically_normal()
								.generic_string() != expected->second;
					}),
				m_terrain_proxies.end());
			for (auto request = m_pending_terrain_loads.begin();
				request != m_pending_terrain_loads.end();)
			{
				if (active_path_set.find(request->first) == active_path_set.end())
				{
					request = m_pending_terrain_loads.erase(request);
				}
				else
				{
					++request;
				}
			}
			pending_result.drawable_published_view = std::any_of(
				m_terrain_proxies.begin(),
				m_terrain_proxies.end(),
				[](const std::shared_ptr<RenderTerrainProxy>& proxy)
				{
					return proxy && proxy->get_render_asset() &&
						proxy->get_render_asset()->published_view() != nullptr;
				});
			m_last_terrain_resolve_result = pending_result;
			return pending_result;
		}

		std::vector<std::shared_ptr<RenderTerrainProxy>> rebuilt_proxies{};
		rebuilt_proxies.reserve(terrain_entities.size());
		TerrainSceneResolveResult render_result{};
		for (const ResolvedTerrain& terrain : resolved_terrains)
		{
			std::string render_error{};
			std::shared_ptr<TerrainRenderAsset> render_asset =
				render_asset_manager.request_terrain_asset(
					terrain.canonical_path,
					terrain.snapshot,
					&render_error);
			if (!render_asset)
			{
				return MakeFailure(
					terrain.canonical_path,
					render_error.empty() ?
						"RenderAssetManager rejected the Terrain snapshot." :
						render_error,
					terrain.snapshot->content_generation);
			}

			const std::shared_ptr<const TerrainPublishedRenderView>
				published_view = render_asset->published_view();
			const std::shared_ptr<const TerrainAssetSnapshot> proxy_snapshot =
				published_view && published_view->snapshot ?
					published_view->snapshot : terrain.snapshot;
			auto proxy = std::make_shared<RenderTerrainProxy>();
			if (!proxy->initialize(
					terrain.desc.entity_id,
					proxy_snapshot,
					terrain.desc.world_transform,
					terrain.desc.terrain.visible,
					terrain.desc.terrain.casts_shadow,
					terrain.desc.terrain.receives_shadow,
					render_asset,
					terrain.canonical_path))
			{
				return MakeFailure(
					terrain.canonical_path,
					"RenderTerrainProxy rejected the Terrain snapshot.",
					terrain.snapshot->content_generation,
					published_view != nullptr);
			}
			rebuilt_proxies.push_back(std::move(proxy));

			const std::shared_ptr<const TerrainAssetSnapshot> accepted_snapshot =
				render_asset->accepted_snapshot();
			const bool retains_older_publication = published_view &&
				accepted_snapshot &&
				published_view->snapshot != accepted_snapshot;
			const TerrainRenderReadiness readiness =
				render_asset->readiness();
			if (!render_error.empty() ||
				readiness == TerrainRenderReadiness::Failed)
			{
				render_result.status = TerrainSceneResolveStatus::Failed;
				render_result.asset_path = terrain.canonical_path;
				render_result.diagnostic = render_error.empty() ?
					render_asset->get_last_error() : std::move(render_error);
				render_result.content_generation =
					terrain.snapshot->content_generation;
				render_result.drawable_published_view =
					published_view != nullptr;
			}
			else if (render_result.status != TerrainSceneResolveStatus::Failed &&
				(retains_older_publication || !published_view) &&
				readiness == TerrainRenderReadiness::Pending)
			{
				render_result.status = TerrainSceneResolveStatus::Pending;
				render_result.asset_path = terrain.canonical_path;
				render_result.content_generation =
					terrain.snapshot->content_generation;
				render_result.drawable_published_view =
					published_view != nullptr;
			}
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_terrain_proxies = std::move(rebuilt_proxies);
			for (const std::string& path : active_paths)
			{
				m_pending_terrain_loads.erase(path);
			}
			m_last_terrain_resolve_result = std::move(render_result);
			for (const ResolvedTerrain& terrain : resolved_terrains)
			{
				m_last_terrain_resolve_result.content_generation = std::max(
					m_last_terrain_resolve_result.content_generation,
					terrain.snapshot->content_generation);
			}
			return m_last_terrain_resolve_result;
		}
	}

	TerrainSceneResolveResult RenderScene::last_terrain_resolve_result() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_last_terrain_resolve_result;
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
			out_frame.terrain_resolve_status =
				m_last_terrain_resolve_result.status;
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
			if (!terrain || !terrain->is_visible())
			{
				continue;
			}
			VisibleTerrainFrame visible_terrain = terrain->make_visible_frame();
			if (!visible_terrain.asset_snapshot ||
				(out_frame.terrain_resolve_status ==
					TerrainSceneResolveStatus::Pending &&
					!visible_terrain.published_view) ||
				!intersects_frustum(visible_terrain.world_bounds, view))
			{
				continue;
			}
			out_frame.terrains.push_back(std::move(visible_terrain));
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
