#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderAssetManager.h"
#include "Function/Render/SceneProxy.h"
#include <memory>
#include <mutex>
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
		glm::vec3 pad0{ 0.0f };
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
		std::vector<VisibleStaticMeshDraw> static_mesh_draws{};
		std::vector<VisibleLightData> lights{};
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
		bool build_visible_render_frame(
			uint64_t frame_index,
			const SceneView& view,
			VisibleRenderFrame& out_frame) const;
		bool build_visible_light_frame(VisibleRenderFrame& out_frame) const;

		// Returns a snapshot of the current primitive list. Safe to call from any
		// thread; the caller owns the returned vector and is unaffected by later
		// rebuilds. Each shared_ptr keeps the proxy alive for the snapshot's lifetime.
		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> get_static_mesh_primitives_snapshot() const;

	private:
		uint64_t m_next_primitive_id = 1;
		std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> m_static_mesh_primitives{};
		std::vector<VisibleLightData> m_lights{};
		mutable std::mutex m_mutex{};
	};
}
