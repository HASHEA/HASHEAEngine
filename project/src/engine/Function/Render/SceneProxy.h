#pragma once

#include "Base/hcore.h"
#include "Function/Render/StaticMeshRenderAsset.h"
#include "Function/Scene/Scene.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace AshEngine
{
	struct ASH_API PrimitiveBounds
	{
		bool is_valid = false;
		glm::vec3 local_min{ 0.0f };
		glm::vec3 local_max{ 0.0f };
		glm::vec3 world_min{ 0.0f };
		glm::vec3 world_max{ 0.0f };
		glm::vec3 center{ 0.0f };
		glm::vec3 extents{ 0.0f };
	};

	class ASH_API SceneProxy
	{
	public:
		virtual ~SceneProxy() = default;
	};

	class ASH_API PrimitiveSceneProxy : public SceneProxy
	{
	public:
		virtual ~PrimitiveSceneProxy() = default;

	public:
		EntityId get_entity_id() const;
		uint64_t get_primitive_id() const;
		const glm::mat4& get_world_transform() const;
		const PrimitiveBounds& get_bounds() const;
		uint32_t get_layer_mask() const;
		SceneMobility get_mobility() const;
		bool is_visible() const;

	protected:
		EntityId m_entity_id = 0;
		uint64_t m_primitive_id = 0;
		glm::mat4 m_world_transform{ 1.0f };
		PrimitiveBounds m_bounds{};
		uint32_t m_layer_mask = k_default_scene_layer_mask;
		SceneMobility m_mobility = SceneMobility::Static;
		bool m_visible = true;
	};

	class ASH_API StaticMeshPrimitiveProxy final : public PrimitiveSceneProxy
	{
	public:
		StaticMeshPrimitiveProxy() = default;

	public:
		void initialize(
			uint64_t primitive_id,
			const SceneMeshExtractionDesc& desc,
			const std::shared_ptr<StaticMeshRenderAsset>& render_asset,
			const SceneMeshBounds& local_bounds,
			std::vector<ResolvedStaticMeshSection> sections);

		const std::shared_ptr<StaticMeshRenderAsset>& get_render_asset() const;
		const std::vector<ResolvedStaticMeshSection>& get_sections() const;
		void update_world_transform(const glm::mat4& world_transform);

	private:
		void update_bounds(const SceneMeshBounds& local_bounds);

	private:
		std::shared_ptr<StaticMeshRenderAsset> m_render_asset = nullptr;
		std::vector<ResolvedStaticMeshSection> m_sections{};
		SceneMeshBounds m_local_bounds{};
	};
}
