#include "Function/Render/RenderAssetManager.h"

#include "Base/hlog.h"
#include "Function/Render/Renderer.h"

namespace AshEngine
{
	namespace
	{
		static auto resolve_section_color(const MaterialSlot* slot) -> glm::vec4
		{
			return slot ? slot->base_color_factor : glm::vec4(1.0f);
		}
	}

	void RenderAssetManager::initialize(AssetDatabase* asset_database, Renderer* renderer)
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_asset_database = asset_database;
		m_renderer = renderer;
	}

	void RenderAssetManager::shutdown()
	{
		std::scoped_lock<std::mutex> lock(m_mutex);
		m_static_mesh_assets.clear();
		m_asset_database = nullptr;
		m_renderer = nullptr;
	}

	std::shared_ptr<StaticMeshRenderAsset> RenderAssetManager::request_static_mesh_asset(const std::string& asset_path, uint32_t mesh_index)
	{
		ASH_PROCESS_GUARD_RETURN(std::shared_ptr<StaticMeshRenderAsset>, result, nullptr, nullptr);
		ASH_PROCESS_ERROR(m_asset_database);
		ASH_PROCESS_ERROR(!asset_path.empty());

		const std::string key = make_static_mesh_key(asset_path, mesh_index);
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			const auto found = m_static_mesh_assets.find(key);
			if (found != m_static_mesh_assets.end())
			{
				result = found->second;
				break;
			}
		}

		result = std::make_shared<StaticMeshRenderAsset>();
		result->asset_path = asset_path;
		result->mesh_index = mesh_index;
		result->state = StaticMeshRenderAssetState::Loading;
		ASH_PROCESS_ERROR(populate_cpu_mesh_asset(result));

		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_static_mesh_assets[key] = result;
		}
		ASH_PROCESS_GUARD_RETURN_END(result, nullptr);
	}

	bool RenderAssetManager::finalize_pending_static_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);

		std::scoped_lock<std::mutex> asset_lock(asset->mutex);
		if (asset->state == StaticMeshRenderAssetState::GpuReady)
		{
			break;
		}
		ASH_PROCESS_ERROR(asset->state == StaticMeshRenderAssetState::CpuReady);
		ASH_PROCESS_ERROR(create_gpu_mesh_resource(asset));
		ASH_PROCESS_GUARD_RETURN_END(bResult, false);
	}

	void RenderAssetManager::finalize_pending_assets()
	{
		std::vector<std::shared_ptr<StaticMeshRenderAsset>> assets{};
		{
			std::scoped_lock<std::mutex> lock(m_mutex);
			assets.reserve(m_static_mesh_assets.size());
			for (const auto& [key, asset] : m_static_mesh_assets)
			{
				(void)key;
				assets.push_back(asset);
			}
		}

		for (const std::shared_ptr<StaticMeshRenderAsset>& asset : assets)
		{
			if (!asset)
			{
				continue;
			}
			finalize_pending_static_mesh_asset(asset);
		}
	}

	AssetDatabase* RenderAssetManager::get_asset_database() const
	{
		return m_asset_database;
	}

	Renderer* RenderAssetManager::get_renderer() const
	{
		return m_renderer;
	}

	std::string RenderAssetManager::make_static_mesh_key(const std::string& asset_path, uint32_t mesh_index)
	{
		return asset_path + "#" + std::to_string(mesh_index);
	}

	bool RenderAssetManager::populate_cpu_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);
		ASH_PROCESS_ERROR(m_asset_database);

		std::shared_ptr<const Model> model{};
		ASH_PROCESS_ERROR(m_asset_database->load_model_by_path(asset->asset_path, model));
		ASH_PROCESS_ERROR(model);

		const Mesh* mesh = get_model_mesh_by_index(*model, asset->mesh_index);
		ASH_PROCESS_ERROR(mesh);
		ASH_PROCESS_ERROR(mesh->has_geometry());

		asset->name = mesh->name.empty() ? asset->asset_path : mesh->name;
		asset->bounds.is_valid = true;
		asset->bounds.local_min = mesh->bounds_min;
		asset->bounds.local_max = mesh->bounds_max;
		asset->vertices = mesh->vertices;
		asset->indices = mesh->indices;
		asset->material_slots = model->material_slots;
		asset->sections.clear();
		asset->sections.reserve(mesh->sections.size());
		for (const MeshSection& source_section : mesh->sections)
		{
			StaticMeshRenderSection section{};
			section.first_index = source_section.index_offset;
			section.index_count = source_section.index_count;
			section.material_slot = source_section.material_slot;
			section.topology = source_section.topology;
			section.base_color_factor = resolve_section_color(get_model_material_slot_by_index(*model, source_section.material_slot));
			asset->sections.push_back(std::move(section));
		}
		if (asset->sections.empty())
		{
			StaticMeshRenderSection section{};
			section.first_index = 0;
			section.index_count = static_cast<uint32_t>(asset->indices.size());
			section.material_slot = k_invalid_material_slot;
			section.topology = MeshPrimitiveTopology::Triangles;
			section.base_color_factor = glm::vec4(1.0f);
			asset->sections.push_back(std::move(section));
		}

		asset->last_error.clear();
		asset->state = StaticMeshRenderAssetState::CpuReady;
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			asset->state = StaticMeshRenderAssetState::Failed;
			if (asset->last_error.empty())
			{
				asset->last_error = "Failed to populate static mesh CPU data.";
			}
		}
		return bResult;
	}

	bool RenderAssetManager::create_gpu_mesh_resource(const std::shared_ptr<StaticMeshRenderAsset>& asset)
	{
		ASH_PROCESS_GUARD_RETURN(bool, bResult, true, false);
		ASH_PROCESS_ERROR(asset);
		ASH_PROCESS_ERROR(m_renderer);
		ASH_PROCESS_ERROR(!asset->vertices.empty());
		ASH_PROCESS_ERROR(!asset->indices.empty());

		auto resource = std::make_shared<StaticMeshRenderResource>();
		resource->vertex_buffer = m_renderer->create_vertex_buffer({
			static_cast<uint32_t>(asset->vertices.size() * sizeof(MeshVertex)),
			static_cast<uint32_t>(sizeof(MeshVertex)),
			false,
			asset->vertices.data(),
			asset->name.c_str()
		});
		ASH_PROCESS_ERROR(resource->vertex_buffer);

		resource->index_buffer = m_renderer->create_index_buffer({
			static_cast<uint32_t>(asset->indices.size() * sizeof(uint32_t)),
			RenderIndexFormat::UInt32,
			false,
			asset->indices.data(),
			asset->name.c_str()
		});
		ASH_PROCESS_ERROR(resource->index_buffer);

		resource->vertex_count = static_cast<uint32_t>(asset->vertices.size());
		resource->index_count = static_cast<uint32_t>(asset->indices.size());
		resource->index_format = RenderIndexFormat::UInt32;

		asset->resource = resource;
		asset->last_error.clear();
		asset->state = StaticMeshRenderAssetState::GpuReady;
		ASH_PROCESS_GUARD_END(bResult, false);
		if (!bResult)
		{
			asset->state = StaticMeshRenderAssetState::Failed;
			if (asset->last_error.empty())
			{
				asset->last_error = "Failed to create static mesh GPU resources.";
			}
			HLogError(
				"RenderAssetManager failed to create GPU resource for '{}' mesh {}.",
				asset->asset_path,
				asset->mesh_index);
		}
		return bResult;
	}
}
