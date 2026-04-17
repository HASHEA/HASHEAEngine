#pragma once

#include "Base/hcore.h"
#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/StaticMeshRenderAsset.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace AshEngine
{
	class ASH_API RenderAssetManager
	{
	public:
		RenderAssetManager() = default;

	public:
		void initialize(AssetDatabase* asset_database, Renderer* renderer);
		void shutdown();

		std::shared_ptr<StaticMeshRenderAsset> request_static_mesh_asset(const std::string& asset_path, uint32_t mesh_index);
		bool finalize_pending_static_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset);
		void finalize_pending_assets();

		AssetDatabase* get_asset_database() const;
		Renderer* get_renderer() const;

	private:
		static std::string make_static_mesh_key(const std::string& asset_path, uint32_t mesh_index);
		bool populate_cpu_mesh_asset(const std::shared_ptr<StaticMeshRenderAsset>& asset);
		bool create_gpu_mesh_resource(const std::shared_ptr<StaticMeshRenderAsset>& asset);

	private:
		AssetDatabase* m_asset_database = nullptr;
		Renderer* m_renderer = nullptr;
		std::mutex m_mutex{};
		std::unordered_map<std::string, std::shared_ptr<StaticMeshRenderAsset>> m_static_mesh_assets{};
	};
}
