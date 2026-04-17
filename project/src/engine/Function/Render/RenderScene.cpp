#include "Function/Render/RenderScene.h"

#include "Function/Render/SceneView.h"
#include "Function/Render/Visibility.h"

namespace AshEngine
{
	bool RenderScene::rebuild_from_scene(Scene& scene, RenderAssetManager& render_asset_manager)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		m_static_mesh_primitives.clear();
		m_next_primitive_id = 1;
		ASH_PROCESS_ERROR(scene.is_valid());
		ASH_PROCESS_ERROR(render_asset_manager.get_asset_database() != nullptr);

		AssetDatabase& asset_database = *render_asset_manager.get_asset_database();
		for (const SceneMeshExtractionDesc& mesh_desc : scene.extract_visible_mesh_entities())
		{
			std::shared_ptr<StaticMeshRenderAsset> render_asset = render_asset_manager.request_static_mesh_asset(mesh_desc.asset_path, mesh_desc.mesh_index);
			ASH_PROCESS_ERROR(render_asset);
			ASH_PROCESS_ERROR(render_asset->is_cpu_ready());

			SceneMeshBounds local_bounds{};
			ASH_PROCESS_ERROR(scene.try_get_mesh_local_bounds(asset_database, MeshComponent{
				mesh_desc.asset_path,
				mesh_desc.mesh_index,
				mesh_desc.visible,
				mesh_desc.mobility,
				mesh_desc.layer_mask
			}, local_bounds));

			auto primitive = std::make_shared<StaticMeshPrimitiveProxy>();
			primitive->initialize(m_next_primitive_id++, mesh_desc, render_asset, local_bounds);
			m_static_mesh_primitives.push_back(std::move(primitive));
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_render_frame(
		uint64_t frame_index,
		const SceneView& view,
		const std::shared_ptr<RenderTarget>& output_target,
		VisibleRenderFrame& out_frame) const
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		out_frame = {};
		ASH_PROCESS_ERROR(view.is_valid);
		ASH_PROCESS_ERROR(output_target != nullptr);

		VisibilityResult visibility{};
		ASH_PROCESS_ERROR(run_visibility_query({ this, &view }, visibility));

		out_frame.frame_index = frame_index;
		out_frame.view = view.view;
		out_frame.projection = view.projection;
		out_frame.view_projection = view.view_projection;
		out_frame.camera_position = view.camera_position;
		out_frame.output_target = output_target;

		for (uint64_t primitive_id : visibility.visible_primitives.primitive_ids)
		{
			for (const std::shared_ptr<StaticMeshPrimitiveProxy>& primitive : m_static_mesh_primitives)
			{
				if (!primitive || primitive->get_primitive_id() != primitive_id)
				{
					continue;
				}

				VisibleStaticMeshDraw draw{};
				draw.primitive_id = primitive->get_primitive_id();
				draw.entity_id = primitive->get_entity_id();
				draw.world_transform = primitive->get_world_transform();
				draw.render_asset = primitive->get_render_asset();
				draw.sections = primitive->get_sections();
				out_frame.static_mesh_draws.push_back(std::move(draw));
				break;
			}
		}

		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	const std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>>& RenderScene::get_static_mesh_primitives() const
	{
		return m_static_mesh_primitives;
	}
}
