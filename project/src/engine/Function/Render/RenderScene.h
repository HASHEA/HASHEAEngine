#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/SceneProxy.h"
#include "Function/Scene/SceneConfig.h"
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct SceneView;

	struct ASH_API VisibleStaticMeshDraw
	{
		uint64_t primitive_id = 0;
		EntityId entity_id = 0;
		glm::mat4 world_transform{ 1.0f };
		std::shared_ptr<StaticMeshRenderAsset> render_asset = nullptr;
		std::vector<ResolvedStaticMeshSection> sections{};
		PrimitiveBounds bounds{};
		SceneMobility mobility = SceneMobility::Static;
	};

	struct ASH_API VisibleLightData
	{
		EntityId entity_id = 0;
		LightType type = LightType::Directional;
		glm::vec3 position_ws{ 0.0f };
		float range = 0.0f;
		glm::vec3 direction_ws{ 0.0f, 0.0f, 1.0f };
		float inner_cone_cos = 1.0f;
		glm::vec3 color{ 1.0f };
		float outer_cone_cos = 1.0f;
		float intensity = 1.0f;
		bool casts_shadow = true;
		bool sunlight = false;
		uint32_t shadow_priority = 128;
		float shadow_distance = 0.0f;
		uint32_t shadow_cascade_count = 0;
		float near_shadow_distance = 0.0f;
	};

	struct ASH_API VisibleEnvironmentData
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

	struct ASH_API VisibleRenderFrame
	{
		uint64_t frame_index = 0;
		SceneView* debug_view = nullptr;
		glm::mat4 view{ 1.0f };
		glm::mat4 projection{ 1.0f };
		glm::mat4 view_projection{ 1.0f };
		glm::vec3 camera_position{ 0.0f };
		bool reverse_z = false;
		glm::vec2 taa_jitter_ndc{ 0.0f, 0.0f };
		glm::vec2 taa_previous_jitter_ndc{ 0.0f, 0.0f };
		bool taa_enabled = false;
		uint64_t static_scene_revision = 0;
		uint64_t transform_scene_revision = 0;
		uint64_t light_scene_revision = 0;
		std::vector<VisibleStaticMeshDraw> static_mesh_draws{};
		std::vector<VisibleStaticMeshDraw> shadow_caster_static_mesh_draws{};
		std::vector<VisibleLightData> lights{};
		std::optional<VisibleEnvironmentData> environment{};
		SceneRenderConfig render_config{};
	};

	class ASH_API RenderScene
	{
	public:
		RenderScene() = default;
		RenderScene(const RenderScene& other);
		RenderScene(RenderScene&& other) noexcept;
		RenderScene& operator=(const RenderScene& other);
		RenderScene& operator=(RenderScene&& other) noexcept;
		~RenderScene() = default;

	public:
		bool rebuild_from_scene(Scene& scene, RenderAssetManager& render_asset_manager);
		bool update_transforms_from_scene(const Scene& scene);
		bool rebuild_lights_from_scene(const Scene& scene);
		bool rebuild_environment_from_scene(const Scene& scene);
		bool rebuild_render_config_from_scene(const Scene& scene);
		bool build_visible_render_frame(
			uint64_t frame_index,
			const SceneView& view,
			VisibleRenderFrame& out_frame,
			uint64_t static_scene_revision = 0,
			uint64_t transform_scene_revision = 0,
			uint64_t light_scene_revision = 0) const;
		bool build_visible_light_frame(VisibleRenderFrame& out_frame) const;

		// Returns a snapshot of the current primitive list. Safe to call from any
		// thread; the caller owns the returned vector and is unaffected by later
		// rebuilds. Each shared_ptr keeps the proxy alive for the snapshot's lifetime.
		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> get_static_mesh_primitives_snapshot() const;

	private:
		uint64_t m_next_primitive_id = 1;
		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> m_static_mesh_primitives{};
		std::vector<VisibleLightData> m_lights{};
		std::optional<VisibleEnvironmentData> m_environment{};
		SceneRenderConfig m_render_config = make_default_scene_render_config();
		mutable std::mutex m_mutex{};
	};
}
