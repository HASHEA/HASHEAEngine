#include "Function/Render/RenderScene.h"

#include "Function/Render/SceneView.h"
#include "Function/Render/Visibility.h"
#include <unordered_map>

namespace AshEngine
{
	RenderScene::RenderScene(const RenderScene& other)
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = other.m_static_mesh_primitives;
	}

	RenderScene::RenderScene(RenderScene&& other) noexcept
	{
		std::scoped_lock<std::mutex> lock(other.m_mutex);
		m_next_primitive_id = other.m_next_primitive_id;
		m_static_mesh_primitives = std::move(other.m_static_mesh_primitives);
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
		other.m_next_primitive_id = 1;
		return *this;
	}

	bool RenderScene::rebuild_from_scene(Scene& scene, RenderAssetManager& render_asset_manager)
	{
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
			ASH_PROCESS_ERROR(scene.try_get_mesh_local_bounds(asset_database, MeshComponent{
				mesh_desc.asset_path,
				mesh_desc.mesh_index,
				mesh_desc.visible,
				mesh_desc.mobility,
				mesh_desc.layer_mask
			}, local_bounds));

			auto primitive = std::make_shared<StaticMeshPrimitiveProxy>();
			primitive->initialize(next_primitive_id++, mesh_desc, render_asset, local_bounds);
			rebuilt_primitives.push_back(std::move(primitive));
		}

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_static_mesh_primitives = std::move(rebuilt_primitives);
			m_next_primitive_id = next_primitive_id;
		}
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	bool RenderScene::build_visible_render_frame(
		uint64_t frame_index,
		const SceneView& view,
		VisibleRenderFrame& out_frame) const
	{
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

	std::vector<std::shared_ptr<StaticMeshPrimitiveProxy>> RenderScene::get_static_mesh_primitives_snapshot() const
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		return m_static_mesh_primitives;
	}
}
