#pragma once

#include "Base/hcore.h"
#include "Function/Render/SceneProxy.h"
#include "Function/Render/TerrainRenderAsset.h"

#include <memory>

#include <glm/glm.hpp>

namespace AshEngine
{
	struct ASH_API VisibleTerrainFrame
	{
		EntityId entity_id = 0u;
		glm::mat4 world_transform{ 1.0f };
		PrimitiveBounds world_bounds{};
		std::shared_ptr<const TerrainAssetSnapshot> asset_snapshot{};
		std::shared_ptr<TerrainRenderAsset> render_asset{};
		bool casts_shadow = true;
		bool receives_shadow = true;
	};

	class ASH_API RenderTerrainProxy final : public SceneProxy
	{
	public:
		bool initialize(
			EntityId entity_id,
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot,
			const glm::mat4& world_transform,
			bool visible,
			bool casts_shadow,
			bool receives_shadow,
			std::shared_ptr<TerrainRenderAsset> render_asset = nullptr);
		bool replace_snapshot(
			const std::shared_ptr<const TerrainAssetSnapshot>& snapshot);
		bool update_world_transform(const glm::mat4& world_transform);

		EntityId get_entity_id() const;
		bool is_visible() const;
		const PrimitiveBounds& get_bounds() const;
		const std::shared_ptr<const TerrainAssetSnapshot>& get_snapshot() const;
		const std::shared_ptr<TerrainRenderAsset>& get_render_asset() const;
		VisibleTerrainFrame make_visible_frame() const;

	private:
		EntityId m_entity_id = 0u;
		glm::mat4 m_world_transform{ 1.0f };
		PrimitiveBounds m_bounds{};
		std::shared_ptr<const TerrainAssetSnapshot> m_snapshot{};
		std::shared_ptr<TerrainRenderAsset> m_render_asset{};
		bool m_visible = true;
		bool m_casts_shadow = true;
		bool m_receives_shadow = true;
	};
}
